import random
import copy
import os
from datetime import timedelta
from pathlib import Path
from typing import Dict, List, Literal, Optional, Tuple, Union
from enum import Enum, auto
from collections import defaultdict

from tqdm import tqdm
import torch
import torch.nn.functional as F
import transformers

from lm_eval import utils
from lm_eval.models.huggingface import HFLM
from lm_eval.api.model import TemplateLM
from lm_eval.api.registry import register_model
from lm_eval.models.utils import (
    Collator,
    clear_torch_cache,
    configure_pad_token,
    get_dtype,
    pad_and_concat,
    stop_sequences_criteria,
)


eval_logger = utils.eval_logger

"""
This script follows the lm-evaluation-harness guide for adding a new model 
(see: https://github.com/EleutherAI/lm-evaluation-harness/blob/main/docs/model_guide.md)
and adds an interface for Language Models (LM) evaluation using standard datasets
and evaluation metrics 
(see full list at https://github.com/EleutherAI/lm-evaluation-harness/tree/main/lm_eval/tasks).
For example MMLU and accuracy (https://github.com/EleutherAI/lm-evaluation-harness/blob/main/lm_eval/tasks/mmlu/default/_mmlu.yaml).
"""


class ModelClasses(Enum):
    CausalLM = auto()
    Seq2SeqLM = auto()


@register_model("tt", "tenstorrent")
class TenstorrentLM(TemplateLM):
    def __init__(
        self,
        model_backend,
        tokenizer: Optional[
            Union[
                str,
                transformers.PreTrainedTokenizer,
                transformers.PreTrainedTokenizerFast,
            ]
        ] = None,
        model_class: Optional[ModelClasses] = ModelClasses.CausalLM,
        device: Optional[str] = "tt",
        eot_token_id: Optional[int] = None,
        **kwargs,
    ) -> None:
        """
        Override HFLM init function to provide support for Tenstorrent device backend
        """
        super().__init__()
        # set model
        self.model_backend = model_backend
        self.tokenizer = tokenizer
        # for compatability with huggingface interface code
        self.AUTO_MODEL_CLASS = model_class
        self.batch_size = model_backend.batch_size
        self.max_length = model_backend.max_seq_len
        # TODO: handle tokenizer default eot_token_id
        self._eot_token_id = eot_token_id

    @property
    def eot_token_id(self):
        return self._eot_token_id

    def tok_encode(self, string: str, **kwargs) -> List[int]:
        """
        Tokenize a string using the model's tokenizer and return a list of token IDs.
        tok_encode gets called by TemplateLM._encode_pair()
        see: https://github.com/EleutherAI/lm-evaluation-harness/blob/main/lm_eval/api/model.py#L337
        """
        return self.model_backend.tok_encode(string, **kwargs)

    def _encode_pair(self, context: str, continuation: str) -> Tuple[List[int], List[int]]:
        n_spaces = len(context) - len(context.rstrip())
        if n_spaces > 0:
            continuation = context[-n_spaces:] + continuation
            context = context[:-n_spaces]

        model_class = getattr(self, "AUTO_MODEL_CLASS", None)

        if model_class == ModelClasses.CausalLM:
            context_enc = self.tok_encode(context)
            continuation_enc = self.tok_encode(continuation, add_special_tokens=False)
        elif model_class == ModelClasses.Seq2SeqLM:
            raise NotImplementedError
            whole_enc = self.tok_encode(context + continuation)
            context_enc = self.tok_encode(context)
            context_enc_len = len(context_enc)
            continuation_enc = whole_enc[context_enc_len:]
        else:
            raise NotImplementedError

        return context_enc, continuation_enc

    def tok_batch_encode(
        self,
        strings: List[str],
        padding_side: str = "left",
        left_truncate_len: int = None,
        truncation: bool = False,
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        # encode a batch of strings. converts to tensors and pads automatically, unlike tok_encode.
        old_padding_side = self.tokenizer.padding_side
        self.tokenizer.padding_side = padding_side

        add_special_tokens = {}
        if self.AUTO_MODEL_CLASS == transformers.AutoModelForCausalLM:
            add_special_tokens = {"add_special_tokens": False or self.add_bos_token}

        encoding = self.tokenizer(
            strings,
            truncation=truncation,
            padding="longest",
            return_tensors="pt",
            **add_special_tokens,
        )
        if left_truncate_len:
            encoding["input_ids"] = encoding["input_ids"][:, -left_truncate_len:]
            encoding["attention_mask"] = encoding["attention_mask"][:, -left_truncate_len:]
        self.tokenizer.padding_side = old_padding_side

        return encoding["input_ids"], encoding["attention_mask"]

    def _loglikelihood_tokens(
        self,
        requests: List[Tuple[Tuple[str, str], List[int], List[int]]],
        disable_tqdm: bool = False,
        override_bs: int = None,
    ) -> List[Tuple[float, bool]]:
        res = []
        # note: given prefill and decode behaviour, having generic model call function is difficult
        return res

    def loglikelihood(self, requests, disable_tqdm: bool = False) -> List[Tuple[float, bool]]:
        """Compute log-likelihood of generating a continuation from a context.
        Downstream tasks should attempt to use loglikelihood instead of other
        LM calls whenever possible.

        :param requests: list[Instance]
            A list of Instance objects, with property `args` which returns a tuple (context, continuation).
            `context: str`
                Context string. Implementations of LM must be able to handle an
                empty context string.
            `continuation: str`
                The continuation over which log likelihood will be calculated. If
                there is a word boundary, the space should be in the continuation.
                For example, context="hello" continuation=" world" is correct.

        :return: list[tuple[float, bool]]
            A list of pairs (logprob, isgreedy)
            `logprob: float`
                The log probability of `continuation`.
            `isgreedy`:
                Whether `continuation` would be generated by greedy sampling from `context`.
        """
        res = []
        # group by doc_id
        grouped_requests = defaultdict(list)
        for req in requests:
            doc_id = req.doc_id
            grouped_requests[doc_id].append(req)

        # group by batch
        batched_requests = defaultdict(list)
        for i in range(len(grouped_requests)):
            batched_requests[i // self.batch_size].append(grouped_requests[i])

        for batch_idx, req_group in tqdm(batched_requests.items(), disable=disable_tqdm):
            batch_size = len(req_group)
            # continuation_enc_list = [self.tok_encode(req.args[1], add_special_tokens=False) for req in reqs]
            # context_enc_list, continuation_enc_list = zip(
            #     *[self._encode_pair(context=req_list[0].args[0], continuation=req.args[1]) for req_list in req_group]
            # )
            context_enc_list = [self.tok_encode(req_list[0].args[0]) for req_list in req_group]
            continuation_enc_list = [
                [self.tok_encode(req.args[1], add_special_tokens=False)[0]
                for req in req_list
                ]
                for req_list in req_group 
            ]
            cond_ids = torch.tensor(continuation_enc_list)
            self.model_backend.add_users_from_context(context_enc_list)
            self.model_backend.generate_n(n_tokens=1, return_logits=True)
            breakpoint()
            # logits = self.model_backend.get_logits()
            logits = logits[:batch_size, :]
            targets = [req_list[0].doc['answer'] for req_list in req_group]
            target_ids = cond_ids[torch.arange(len(cond_ids)), targets]
            # next: set target indexes high for testing
            # logits[torch.arange(0, len(target_ids)), target_ids] = 1000.0
            # get log conditional likelihoods for all choices
            probs = F.softmax(logits, dim=-1)
            greedy_ids = probs.argmax(dim=-1)
            cond_probs = torch.gather(probs, 1, cond_ids)
            ll_list = torch.log(cond_probs).tolist()
            # set return values for each request by doc_id-choice
            for cond, ll, gid in zip(cond_ids , ll_list, greedy_ids):
                is_greedy = (greedy_ids == target_ids).tolist()
                output = list(zip(ll, is_greedy))
                res.extend(output)
        breakpoint()
        return res

    def loglikelihood_rolling(self, requests, disable_tqdm: bool = False):
        """Compute full log-likelihood of a string, with no truncation, for perplexity computation
        - We will use the full max context length of the model.
        - For inputs that exceed the max context length, we divide the tokenized string into chunks of up to
        the max context length.
        - IMPORTANT: Each document's loglikelihood/perplexity is computed *separately*, unlike other implementations
          which may simply concatenate multiple documents together.
        - IMPORTANT: We maximize the amount of context for each prediction. Specifically, for inputs that we break into
          multiple chunks, the last input will still a full-sized context.
          Example:
            Input tokens: [ 0 1 2 3 4 5 6 7 8 9 ]
            Prefix: BOS/EOS
            Max context length: 4
            Resulting input/prediction pairs:

                INPUT:  BOS   0   1   2
                PRED:     0   1   2   3

                INPUT:    3   4   5   6
                PRED:     4   5   6   7

                INPUT:    5   6   7   8
                PRED:             8   9

          Observe that:
            1. Each token is predicted exactly once
            2. For the last pair, we provide the full context, but only score the last two tokens

        :param requests: list[Instance]
            A list of Instance objects with property `args` which returns a tuple (context,).
            string: str
                String for which we are computing overall loglikelihood
        :return: list[tuple[float]]
            A list of tuples (logprob,)
            logprob: float
                The log probability of `context` conditioned on the BOS/EOS token.
                Can also be overridden for custom cases by `prefix_token_id`.
        """
        loglikelihoods = []

        for (string,) in tqdm([req.args for req in requests], disable=(disable_tqdm or (self.rank != 0))):
            rolling_token_windows = list(
                map(
                    utils.make_disjoint_window,
                    utils.get_rolling_token_windows(
                        token_list=self.tok_encode(string),
                        prefix_token=self.prefix_token_id,
                        max_seq_len=self.max_length,
                        context_len=1,
                    ),
                )
            )

            # TODO: Right now, we pass single EOT token to the Encoder and the full context to the decoder, in seq2seq case
            rolling_token_windows = [(None,) + x for x in rolling_token_windows]

            string_nll = self._loglikelihood_tokens(
                requests=rolling_token_windows,
                disable_tqdm=True,
                override_bs=adaptive_batch_size,
            )

            # discard is_greedy
            string_nll = [x[0] for x in string_nll]

            string_nll = sum(string_nll)
            loglikelihoods.append(string_nll)

        return loglikelihoods

    def generate_until(self, requests, disable_tqdm: bool = False):
        """Generate greedily until a stopping sequence

        :param requests: list[Instance]
            A list of Instance objects with property `args` which returns a tuple (context, gen_kwargs).
            context: str
                Context string
            gen_kwargs: dict
                A dictionary of keyword arguments to pass to the generation function e.g. top_k, until, etc.
        :return: list[str]
            A list of model generated continuations.
            continuation: str
                The generated continuation.
        """
        # TODO
        res = []

        for ctx, _ in tqdm(requests, disable=disable_tqdm):
            res.append("dummy_output")
            assert ctx.strip() != ""

        return res

    def apply_chat_template(self, chat_history: List[Dict[str, str]]) -> str:
        """
        Defines how to transform few-shot examples provided as chat history into a format that can be used as input to the LM.

        :param chat_history: list[dict[str, str]]
            A list of dictionaries with keys 'role' and 'content'.
            Values are strings representing the role name and the content of the message, respectively.
        :return: str
            A string representing the chat history in a format that can be used as input to the LM.
        """
        raise NotImplementedError(
            "To use this model with chat templates, please implement the 'apply_chat_template' method for your model type."
        )

    @property
    def tokenizer_name(self) -> str:
        """Must be defined for LM subclasses which implement Chat Templating.
        Should return the name of the tokenizer or chat template used.
        Used only to properly fingerprint caches when requests are being cached with `--cache_requests`, otherwise not used.
        """
        raise NotImplementedError(
            "To use this model with chat templates, please implement the 'tokenizer_name' property."
        )

    @property
    def chat_template(self) -> str:
        """Must be defined for LM subclasses that implement Chat Templating.
        Should return the structure of the chat template applied to user/assistant messages.
        This is used only to save in the experiment results for reproducibility.
        """
        raise NotImplementedError(
            "To use this model with chat templates, please implement the 'chat_template' property."
        )
