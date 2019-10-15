# Run server (single mode player/wait for connection/enable tracing)
./netris -u -w -f

# Change defeault interval
./netris -w -i 0.1

# Connect clinet
./netris -c localhost

# Run simple robot
./netris -c localhost -r './sr -l'
