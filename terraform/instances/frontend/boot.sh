#!/bin/bash

set -e

git clone -b terraform https://github.com/NBISweden/LocalEGA.git ~/repo
sudo pip3.6 install ~/repo/src

echo "Waiting for database"
until /bin/nc -4 --send-only ega-db 5432 </dev/null &>/dev/null; do /bin/sleep 1; done

echo "Starting the frontend"
sudo systemctl start ega-frontend
sudo systemctl enable ega-frontend

echo "LEGA ready"