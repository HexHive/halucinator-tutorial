#!/bin/bash

halucinator-rehost \
  -c=./config.yaml \
  -a=./addrs.yaml \
  -m=./mem.yaml \
  --log_blocks -n stm32f411re_uart
