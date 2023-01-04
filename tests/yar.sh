#!/bin/bash

./bin/yardb --clog --file=primary.db   2110 &> primary.log   &

./bin/yardb --clog --file=secondary.db 2111 &> secondary.log &

./bin/yardb --clog --file=tertiary.db  2112 &> tertiary.log  &

./bin/yarproxy --clog --replica=http://localhost:2110  --replica=http://localhost:2111  --replica=http://localhost:2112 2113 &

./bin/yarsh http://localhost:2113
