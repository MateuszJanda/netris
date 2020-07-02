Modified version of [Netris by Mark H. Weaver](https://github.com/naclander/netris) - Tetris like game - used as a machine learning environment with features like:
- network communication tracking for supervised learning
- options disabling unnecessary delays for reinforcement learning
- diagnostics

Please read original [README](README) file.

## Additional options added to client
```bash
  -f        Enable tracing to file (%Y%m%d%H%M%S.trace)
  -t        Enable tracing to terminal (/dev/pts/0)
  -u        Force single player mode for network connection
  -d        Replace default message on display
```

## Usage examples
### Run server (single mode player, wait for connection, enable tracing)
```bash
./netris -u -w -f
```

### Change default interval
```bash
./netris -w -i 0.1
```

### Connect client
```bash
./netris -c localhost
```

### Run simple robot
```bash
./netris -c localhost -r './sr -l'
```
