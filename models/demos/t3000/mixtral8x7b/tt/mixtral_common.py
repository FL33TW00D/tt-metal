# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from loguru import logger
import torch
import ttnn
from ttnn import ShardTensorToMesh, ConcatMeshToTensor, ReplicateTensorToMesh
from models.utility_functions import nearest_32
import json


class LightweightModule:
    def __call__(self, *args, **kwargs):
        return self.forward(*args, **kwargs)


# load from json, return as a list
def load_inputs(user_input, batch):
    if isinstance(user_input, str):
        with open(user_input, "r") as f:
            user_input = json.load(f)
    assert len(user_input) >= batch, f"Number of users (batch) must be {batch}!"
    in_prompt = []
    for i in range(batch):
        in_prompt.append(user_input[i]["prompt"])
    return in_prompt


def preprocess_inputs(input_prompts, tokenizer, model_args, dtype, instruct, device_mesh):
    """
    Run tokenizer on inputs, and create embeddings for the first token of each input
    """
    if instruct:
        # Pre append [INST] and post append [/INST] to the encoded prompts if instruct mode
        encoded_prompts = [tokenizer.encode("[INST] " + prompt + " [/INST]") for prompt in input_prompts]
    else:
        encoded_prompts = [tokenizer.encode(prompt) for prompt in input_prompts]

    prompt_lens = [len(x) for x in encoded_prompts]

    # Pad the inputs to the max length prompt
    max_prompt_len = max(prompt_lens)
    input_tokens = torch.full((len(input_prompts), max_prompt_len), tokenizer.pad_id, dtype=torch.int32)

    logger.info(f"# of users: {len(encoded_prompts)}")
    for i, encoded in enumerate(encoded_prompts):
        # Right padding
        input_tokens[i, : len(encoded)] = torch.tensor(encoded).to(input_tokens)

    input_mask_bool = input_tokens != tokenizer.pad_id
    input_mask = input_mask_bool.int()  # from_torch doesn't support bool type

    # convert to ttnn tensor
    # Encoded input tokens need to be uint32 for embedding. Otherwise the dtype conversion to bfloat16 will change the tokenizer ID
    input_tokens_tt = [
        ttnn.from_torch(
            input_tokens[:, i].unsqueeze(0),
            device=device_mesh,
            dtype=ttnn.uint32,
            layout=ttnn.ROW_MAJOR_LAYOUT,
            mesh_mapper=ReplicateTensorToMesh(device_mesh),
        )
        for i in range(max_prompt_len)
    ]
    input_mask_tt = [
        ttnn.from_torch(
            input_mask[:, i].unsqueeze(0),
            device=device_mesh,
            dtype=ttnn.bfloat16,
            layout=ttnn.ROW_MAJOR_LAYOUT,
            mesh_mapper=ReplicateTensorToMesh(device_mesh),
        )
        for i in range(max_prompt_len)
    ]
    return input_tokens_tt, max_prompt_len, input_mask_tt, input_tokens, input_mask_bool


def prepare_inputs_ttnn(x_bsh, hidden_size, current_pos, model_args, device_mesh):
    """
    Prepare inputs for decode mode.
    x: (batch, seq, hidden_dim)
    B: batch (32)
    S: sequence len (1)
    H: dim (4096)
    """
    assert x_bsh.size(2) == hidden_size
    assert len(x_bsh.size()) == 3

    batch = x_bsh.size(0)
    seq_len = x_bsh.size(1)
    assert seq_len == 1, "Only supporting decode mode"

    x_1SBH = x_bsh.view(1, seq_len, batch, hidden_size)

    # input goes to L1
    xs_1SBH = ttnn.from_torch(
        x_1SBH,
        # device=device_mesh,
        dtype=ttnn.bfloat16,
        layout=ttnn.TILE_LAYOUT,
        # memory_config=ttnn.L1_MEMORY_CONFIG,
        mesh_mapper=ReplicateTensorToMesh(device_mesh),
    )

    # Attention mask
    padded_layer_past_len = model_args.max_seq_len  # nearest_32(current_pos + 1)
    attn_mask = torch.zeros(seq_len, 32, 32, padded_layer_past_len)  # [SB4P]
    attn_mask[:, :, :, current_pos + 1 :] = torch.finfo(attn_mask.dtype).min

    if model_args.dummy_weights:
        cache_name = None
    else:
        cache_name = model_args.weight_cache_path(ttnn.bfloat4_b) / (f"attention_mask.{current_pos}.trace")

    attn_mask = ttnn.as_tensor(
        attn_mask,
        # device=device_mesh,
        dtype=ttnn.bfloat4_b,
        layout=ttnn.TILE_LAYOUT,
        # memory_config=ttnn.create_sharded_memory_config(
        #     shape=(32, padded_layer_past_len),
        #     core_grid=ttnn.CoreGrid(y=4, x=8),
        #     strategy=ttnn.ShardStrategy.HEIGHT,
        #     orientation=ttnn.ShardOrientation.ROW_MAJOR,
        #     use_height_and_width_as_shard_shape=True,
        # ),
        mesh_mapper=ReplicateTensorToMesh(device_mesh),
        cache_file_name=cache_name,
    )

    return xs_1SBH, attn_mask


# Sample logits from a distribution
def sample_top_p(probs: torch.Tensor, p: float):
    assert 0 <= p <= 1

    probs_sort, probs_idx = torch.sort(probs, dim=-1, descending=True)
    probs_sum = torch.cumsum(probs_sort, dim=-1)
    mask = probs_sum - probs_sort > p
    probs_sort[mask] = 0.0
    probs_sort.div_(probs_sort.sum(dim=-1, keepdim=True))

    next_token = torch.multinomial(probs_sort, num_samples=1)
    return torch.gather(probs_idx, -1, next_token)


def sample(logits: torch.Tensor, temperature: float, top_p: float):
    if temperature > 0:
        probs = torch.softmax(logits / temperature, dim=-1)
        next_token = sample_top_p(probs.squeeze(), top_p)
    else:
        next_token = torch.argmax(logits, dim=-1)

    return next_token


def cache_attention(device_mesh, state_dict, model_args, current_rot_mat, rot_matrix, seq_start, seq_len, dtype):
    logger.info(f"Caching attention ops for iterations {seq_start} to {seq_start + seq_len}...")
    from models.demos.t3000.mixtral8x7b.tt.mixtral_attention import TtMixtralAttention

    attention_inputs = ttnn.from_torch(
        torch.randn(1, 1, 32, 4096),
        dtype=ttnn.bfloat16,
        layout=ttnn.TILE_LAYOUT,
        device=device_mesh,
        memory_config=ttnn.L1_MEMORY_CONFIG,
        mesh_mapper=ReplicateTensorToMesh(device_mesh),
    )

    tt_attn = TtMixtralAttention(
        device_mesh,
        state_dict,
        model_args,
        layer_num=0,
        dtype=dtype,
    )

    for iter in range(seq_start, seq_start + seq_len):
        logger.info(f"Caching iteration {iter}...")
        if iter > 0:
            current_rot_mat = ttnn.linear(rot_matrix, current_rot_mat)
        pos = iter

        if model_args.dummy_weights:
            cache_name = None
        else:
            cache_name = model_args.weight_cache_path(ttnn.bfloat4_b) / (f"attention_mask.{pos}")

        padded_layer_past_len = min(nearest_32(pos + 1), model_args.sliding_window)
        # 32 on dim 2 for 1 tile (padded n_heads)
        attn_mask = torch.zeros(1, model_args.max_batch_size, 32, padded_layer_past_len)
        attn_mask[:, :, :, pos + 1 :] = torch.finfo(attn_mask.dtype).min

        ATTN_MASK_MEMCFG = ttnn.create_sharded_memory_config(
            shape=(32, padded_layer_past_len),
            core_grid=ttnn.CoreGrid(y=4, x=8),
            strategy=ttnn.ShardStrategy.HEIGHT,
            orientation=ttnn.ShardOrientation.ROW_MAJOR,
            use_height_and_width_as_shard_shape=True,
        )
        attn_mask = ttnn.as_tensor(
            # torch.zeros(1, 1, 32, padded_layer_past_len),
            attn_mask,
            device=device_mesh,
            dtype=ttnn.bfloat4_b,
            layout=ttnn.TILE_LAYOUT,
            memory_config=ATTN_MASK_MEMCFG,
            mesh_mapper=ReplicateTensorToMesh(device_mesh),
            cache_file_name=cache_name,
        )
        _ = tt_attn(
            attention_inputs,
            pos,
            pos + 1,
            attn_mask,
            current_rot_mat,
        )
        # ttnn.deallocate(tt_out[0])

    logger.info("Attention ops cached")


def get_single_rot_mat(dhead, device_mesh, start_pos=0, theta: float = 1000000.0):
    freqs = 1.0 / (theta ** (torch.arange(0, dhead, 2)[: (dhead // 2)].float() / dhead))
    sin_freqs, cos_freqs = torch.sin(freqs), torch.cos(freqs)
    rot_matrix = torch.zeros(dhead, dhead)
    rot_matrix[torch.arange(0, dhead, 2), torch.arange(0, dhead, 2)] = cos_freqs.clone()
    rot_matrix[torch.arange(1, dhead, 2), torch.arange(1, dhead, 2)] = cos_freqs.clone()
    rot_matrix[torch.arange(0, dhead, 2), torch.arange(1, dhead, 2)] = -sin_freqs.clone()
    rot_matrix[torch.arange(1, dhead, 2), torch.arange(0, dhead, 2)] = sin_freqs.clone()
    rot_matrix = rot_matrix.transpose(-1, -2)

    # Support for start_pos different than 0
    if start_pos >= 0:
        freqs = start_pos * freqs
        sin_freqs, cos_freqs = torch.sin(freqs), torch.cos(freqs)
        current_rot_mat = torch.zeros(dhead, dhead)
        current_rot_mat[torch.arange(0, dhead, 2), torch.arange(0, dhead, 2)] = cos_freqs.clone()
        current_rot_mat[torch.arange(1, dhead, 2), torch.arange(1, dhead, 2)] = cos_freqs.clone()
        current_rot_mat[torch.arange(0, dhead, 2), torch.arange(1, dhead, 2)] = -sin_freqs.clone()
        current_rot_mat[torch.arange(1, dhead, 2), torch.arange(0, dhead, 2)] = sin_freqs.clone()

    else:
        current_rot_mat = torch.inverse(rot_matrix)
        for i in range(-(start_pos + 1)):
            current_rot_mat = current_rot_mat @ current_rot_mat
    return ttnn.from_torch(
        current_rot_mat.unsqueeze(0).unsqueeze(0),  # 1,1,head_dim,head_dim
        device=device_mesh,
        dtype=ttnn.bfloat16,
        layout=ttnn.TILE_LAYOUT,
        mesh_mapper=ReplicateTensorToMesh(device_mesh),
    ), ttnn.from_torch(
        rot_matrix.unsqueeze(0).unsqueeze(0),  # 1,1,head_dim,head_dim
        device=device_mesh,
        dtype=ttnn.bfloat16,
        layout=ttnn.TILE_LAYOUT,
        mesh_mapper=ReplicateTensorToMesh(device_mesh),
    )


#     return current_rot_mat, rot_matrix

# a, b = get_single_rot_mat(6, None, start_pos = -2)

# a1, b1 = get_single_rot_mat(6, None, start_pos = 0)
# from models.utility_functions import (
#     comp_pcc,
#     comp_allclose,
# )
# print(b@b@a, comp_pcc(b@b@a, a1, 0.99))
# print(b1@a1, comp_pcc(b@b@b@a, b1@a1, 0.99))
# print(b1@b1@a1, comp_pcc(b@b@b@b@a, b1@b1@a1, 0.99))
