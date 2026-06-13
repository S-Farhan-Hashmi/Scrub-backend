#!/usr/bin/env bash

find . -maxdepth 1 -type f -name "*.c" -print0 |
sort -z |
xargs -0 cat 
