# PraSLE Implementation

There is no master node. Every node is connected over its serial line (through the testbed's `serial_aggregator`) to give it an id, a rank and its neighbors, and to start the election. The radio carries only the election itself and a short link probe before it.



## Layout

```
prasle.py        The only script to run for end to end experiment
testbed.py       booking, flashing and the serial lines, over SSH
topology.py      line, ring and mesh, laid over the measured RSSI
firmware/
  main.c         Shell commands the host uses
  radio.c        node id, UDP send and receive, link probe
  prasle.c       Election algo
RIOT/            RIOT 2026.04.01 as a git submodule
results/         Election results in a single file
```


## Getting started

### 1. Getting RIOT

Inside repository root, run the following:

```
git clone --branch 2026.04.01 --depth 1 https://github.com/RIOT-OS/RIOT.git RIOT
```

### 2. Tools

You need git, make, the ARM GCC toolchain, Python 3.7 or newer and an SSH
client:

```
sudo apt install git make gcc-arm-none-eabi python3 openssh-client
```

Check that the firmware builds:

```
make -C firmware
```

### 3. FIT IoT-LAB access

1. Create an account at <https://www.iot-lab.info/testbed/signup> and add your SSH public key
   to it in the web portal.
2. Check that you can log in to the frontend of the site you will use:
   `ssh <login>@lille.iot-lab.info`
3. Store your testbed credentials on the frontend once, since the testbed tools
   run there:

   ```
   ssh <login>@lille.iot-lab.info iotlab-auth -u <login>
   ```

   Without this, the testbed refuses the booking and flashing commands.

## Running an election

```
./prasle.py --user <login> --nodes 10 --topology mesh
```

| option       | default | meaning                                              |
|--------------|---------|------------------------------------------------------|
| `--user`     |         | your FIT IoT-LAB login                               |
| `--nodes`    | 5       | how many M3 nodes to book                            |
| `--topology` | line    | `line`, `ring` or `mesh`                             |
| `--site`     | lille   | testbed site                                         |
| `--minutes`  | 30      | how long to book the nodes for                       |
| `--exp`      |         | use all nodes of a running experiment instead        |

The script builds the firmware, books and flashes the nodes, measures the links,
runs the election, writes the results and releases the booking.

### Several runs on one booking

Without `--exp`, every call books and releases its own nodes, which takes a few minutes each time. 
To run several elections on the same nodes, book them yourself and pass the experiment id with `--exp`.
The script then skips booking and leaves the nodes booked when it ends. 
Each run still flashes the nodes again, so every election starts from a fresh boot with new random ranks.
`--nodes` and `--minutes` are ignored, because the run uses every node in that experiment.

#### Example:

1. Book 10 nodes for an hour:

   ```
   ssh username@lille.iot-lab.info iotlab-experiment submit -n prasle -d 60 -l 10,archi=m3:at86rf231+site=lille
   {
       "id": 454285
   }
   ```

2. Wait for it to be running:

   ```
   ssh username@lille.iot-lab.info iotlab-experiment wait -i 454285
   ```

3. Run as many elections as you like. Each writes its own results file:

   ```
   ./prasle.py --user username --topology ring --exp 454285
   ./prasle.py --user username --topology mesh --exp 454285
   ./prasle.py --user username --topology line --exp 454285
   ```

4. Release the nodes when you are done, since the testbed is shared:

   ```
   ssh username@lille.iot-lab.info iotlab-experiment stop -i 454285
   ```


## Results file

```
# PraSLE line, 5 nodes, K=4, T=50 ms, lille experiment 454270, started 2026-10-13 12:26:34 UTC
# expected leader 100
# node rank neighbors leader rounds time_ms sent received cpu_pct
100 1 101,102 100 5 245 2 4 3.4
101 3 100 100 5 214 2 1 2.2
102 2 100,104 100 5 232 4 4 6.4
103 5 104 100 5 202 4 3 4.0
104 4 102,103 100 5 220 6 6 7.6
```

| column     | meaning                                                         |
|------------|-----------------------------------------------------------------|
| `rounds`   | rounds the node ran                                             |
| `time_ms`  | from the node's start to its last round, on its own clock       |
| `sent`     | election frames this node sent                                  |
| `received` | election frames it received from its neighbors, including duplicates |
| `cpu_pct`  | share of that time the CPU was busy, from RIOT's schedstatistics |



## Energy

It is measured by the testbed. Attach a monitoring profile to the nodes before the election, for
example from the frontend:

```
iotlab-node --update-profile <profile> -i <experiment>
```

and after the run fetch `~/.iot-lab/<experiment>/consumption/` from the frontend. 
The start time in the results file and each node's `time_ms` give the window to cut out of the traces.
