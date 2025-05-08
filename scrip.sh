#!/bin/bash
for lang in C C%2B%2B; do
  for i in {1..1}; do
    curl "https://api.github.com/search/repositories?q=language:$lang&sort=stars&order=desc&per_page=100&page=$i" | grep html_url | awk 'NR % 2 == 0' | awk -F'"' '{print $4}' >>repos.txt
  done
done
