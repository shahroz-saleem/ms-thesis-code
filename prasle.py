#!/usr/bin/env python3

import argparse
import os
import random
import subprocess
import time
from collections import Counter
from datetime import datetime, timezone

import topology
from testbed import Serial, Testbed, board_number

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.join(HERE, "firmware")
ELF = os.path.join(FIRMWARE, "bin", "iotlab-m3", "prasle.elf")
RESULTS = os.path.join(HERE, "results")
PROBE_SPACING_S = 0.2


def build_firmware():
    print("Building the firmware")
    result = subprocess.run(["make", "-C", FIRMWARE, "-j4"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"the build failed:\n{result.stdout[-3000:]}{result.stderr[-3000:]}")


def assign_ids(serial, boards):
    commands = {board: f"id {board_number(board)}" for board in boards}
    serial.ask(commands, "id", expected=commands, attempts=5)


def measure_links(serial, boards):
    """Each node broadcasts probes, and reports the average RSSI it heard"""
    for board in boards:
        serial.send(board, "probe")
        time.sleep(PROBE_SPACING_S)
    time.sleep(1)
    answers = serial.ask({board: "heard" for board in boards}, "heard")
    rssi = {}
    for board, text in answers.items():
        pairs = (item.split(":") for item in text.split()[1:])
        rssi[board_number(board)] = {int(peer): int(value) for peer, value in pairs}
    return rssi


def configure(serial, neighbors, ranks, k_rounds, round_ms):
    commands, expected = {}, {}
    for node, peers in neighbors.items():
        peer_list = ",".join(map(str, peers))
        commands[f"m3-{node}"] = f"setup {ranks[node]} {k_rounds} {round_ms} {peer_list}"
        expected[f"m3-{node}"] = (f"setup rank={ranks[node]} k={k_rounds} t={round_ms} "
                                  f"neighbors={peer_list}")
    serial.ask(commands, "setup", expected=expected)


def elect(serial, boards, k_rounds, round_ms):
    serial.send_to_all("start")
    timeout = (k_rounds + 3) * round_ms / 1000 + 10
    answers = serial.wait_for(set(boards), "done", timeout)
    return {board: dict(item.split("=", 1) for item in text.split()[1:])
            for board, text in answers.items()}


def describe(results, boards, expected):
    leaders = Counter(int(r["leader"]) for r in results.values())
    slowest = max((int(r["time"]) for r in results.values()), default=0) / 1000
    sent = sum(int(r["sent"]) for r in results.values())
    if leaders == Counter({expected: len(boards)}):
        print(f"m3-{expected} was elected by all {len(boards)} nodes in {slowest:.2f} s, "
              f"with {sent} messages sent")
    else:
        votes = ", ".join(f"m3-{leader} ({count})" for leader, count in leaders.most_common())
        print(f"Expected m3-{expected}, but the nodes elected {votes or 'nobody'}")
    silent = [board for board in boards if board not in results]
    if silent:
        print(f"No result from {', '.join(silent)}")


def save(path, header, boards, neighbors, ranks, results):
    columns = ("leader", "rounds", "time", "sent", "received", "cpu")
    with open(path, "w") as out:
        out.write(header)
        out.write("# node rank neighbors leader rounds time_ms sent received cpu_pct\n")
        for board in boards:
            node = board_number(board)
            result = results.get(board, {})
            values = [result.get(column, "-") for column in columns]
            peers = ",".join(map(str, neighbors[node]))
            out.write(" ".join([str(node), str(ranks[node]), peers, *values]) + "\n")


def run(testbed, exp, shape):
    print(f"Flashing the nodes of experiment {exp}")
    boards = testbed.flash(exp, ELF)
    n = len(boards)
    if n < (3 if shape == "ring" else 2):
        raise RuntimeError(f"only {n} nodes took the firmware, too few for a {shape}")

    serial = Serial(testbed, exp)
    try:
        assign_ids(serial, boards)
        print(f"Measuring the radio links between {n} nodes")
        rssi = measure_links(serial, boards)

        nodes = [board_number(board) for board in boards]
        order = topology.arrange(nodes, shape, rssi)
        weakest = topology.weakest_link(order, shape, rssi)
        if weakest >= topology.UNHEARD:
            raise RuntimeError(f"some links of the {shape} were never heard, try another booking")
        neighbors = topology.neighbors(order, shape)

        # K is the diameter of the designed graph, T grows by 10 ms per node.
        k_rounds = topology.diameter(shape, n)
        round_ms = 10 * n
        print(f"Built a {shape} with K={k_rounds} and T={round_ms} ms, "
              f"weakest link {-weakest:.0f} dBm")

        ranks = dict(zip(nodes, random.sample(range(1, n + 1), n)))
        expected = min(nodes, key=ranks.get)
        configure(serial, neighbors, ranks, k_rounds, round_ms)

        print("Starting the election")
        started = datetime.now(timezone.utc)
        results = elect(serial, boards, k_rounds, round_ms)
        describe(results, boards, expected)
    finally:
        serial.close()

    os.makedirs(RESULTS, exist_ok=True)
    path = os.path.join(RESULTS, f"{shape}{n}-{started:%Y%m%d-%H%M%S}.txt")
    header = (f"# PraSLE {shape}, {n} nodes, K={k_rounds}, T={round_ms} ms, "
              f"{testbed.site} experiment {exp}, started {started:%Y-%m-%d %H:%M:%S} UTC\n"
              f"# expected leader {expected}\n")
    save(path, header, boards, neighbors, ranks, results)
    print(f"Results are in {os.path.relpath(path)}")


def main():
    parser = argparse.ArgumentParser(description="Run one PraSLE leader election on FIT IoT-LAB.")
    parser.add_argument("--user", required=True, help="your FIT IoT-LAB login")
    parser.add_argument("--site", default="lille", help="testbed site (default: lille)")
    parser.add_argument("--nodes", type=int, default=5, help="how many M3 nodes to book (default: 5)")
    parser.add_argument("--topology", choices=("line", "ring", "mesh"), default="line")
    parser.add_argument("--minutes", type=int, default=30, help="booking length (default: 30)")
    parser.add_argument("--exp", type=int,
                        help="use all nodes of this running experiment instead of booking new ones")
    args = parser.parse_args()

    build_firmware()
    testbed = Testbed(args.user, args.site)
    exp = args.exp
    if exp is None:
        print(f"Booking {args.nodes} M3 nodes at {args.site} for {args.minutes} minutes")
        exp = testbed.book(f"prasle-{args.topology}{args.nodes}", args.nodes, args.minutes)
    try:
        run(testbed, exp, args.topology)
    finally:
        if args.exp is None:
            testbed.release(exp)
            print(f"Experiment {exp} released")


if __name__ == "__main__":
    main()
