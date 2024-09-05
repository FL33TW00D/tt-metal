#/bin/bash

set -eo pipefail

run_common_func_tests() {
  # working on both n150 and n300

  # Falcon7B
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings -q -s --input-method=cli --cli-input="YOUR PROMPT GOES HERE!"  models/demos/wormhole/falcon7b/demo_wormhole.py::test_demo[wormhole_b0-True-user_input0-1-default_mode_1024_stochastic]
  # llama3.1-8B
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto models/demos/wormhole/llama31_8b/demo/demo.py --timeout 600
  # Mistral7B
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto models/demos/wormhole/mistral7b/demo/demo.py --timeout 420

  # Bert
  pytest -n auto --disable-warnings models/demos/metal_BERT_large_11/demo/demo.py -k batch_7
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings models/demos/metal_BERT_large_11/demo/demo.py -k batch_8

  # Resnet
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings models/demos/ttnn_resnet/demo/demo.py
}

run_common_perf_tests(){
  # working on both n150 and n300

  # llama3.1-8B
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto models/demos/wormhole/llama31_8b/demo/demo_with_prefill.py --timeout 600

  # Mistral7B
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto models/demos/wormhole/mistral7b/demo/demo_with_prefill.py --timeout 420

  # Mamba
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings -q -s --input-method=json --input-path='models/demos/wormhole/mamba/demo/prompts.json' models/demos/wormhole/mamba/demo/demo.py --timeout 420
}

run_n150_tests(){
  run_common_func_tests
  run_common_perf_tests

  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings --input-path="models/demos/wormhole/stable_diffusion/demo/input_data.json" models/demos/wormhole/stable_diffusion/demo/demo.py::test_demo --timeout 900
}

run_n300_func_tests(){
  run_common_func_tests
}

run_n300_perf_tests(){
  run_common_perf_tests

  # Falcon7b (perf verification for 128/1024/2048 seq lens and output token verification)
  WH_ARCH_YAML=wormhole_b0_80_arch_eth_dispatch.yaml pytest -n auto --disable-warnings -q -s --input-method=json --input-path='models/demos/falcon7b_common/demo/input_data.json' models/demos/wormhole/falcon7b/demo_wormhole.py
}

main() {
  # For CI pipeline - source func commands but don't execute tests if not invoked directly
  if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    echo "Script is being sourced, not executing main function"
    return 0
  fi

  if [[ -z "$TT_METAL_HOME" ]]; then
    echo "Must provide TT_METAL_HOME in environment" 1>&2
    exit 1
  fi

  if [[ -z "$ARCH_NAME" ]]; then
    echo "Must provide ARCH_NAME in environment" 1>&2
    exit 1
  fi

  # Run all tests
  cd $TT_METAL_HOME
  export PYTHONPATH=$TT_METAL_HOME

  # Insert tests for running locally (currently none since script is only sourced for CI)
}

main "$@"
