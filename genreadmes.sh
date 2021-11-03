#!/bin/bash

pushd "01-firststeps"
pandoc README.md > README.html
popd

pushd "02-interact"
pandoc README.md > README.html
popd

pushd "03-bugs"
pandoc README.md > README.html
popd

pandoc README.md > README.html

