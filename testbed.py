import json
import queue
import subprocess
import threading
import time


class Testbed:
    def __init__(self, user, site):
        self.host = f"{user}@{site}.iot-lab.info"
        self.site = site

    def run(self, command, timeout=600):
        result = subprocess.run(["ssh", self.host, command], capture_output=True,
                                text=True, timeout=timeout)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or result.stdout.strip())
        return result.stdout

    def book(self, name, nodes, minutes):
        reply = self.run(f"iotlab-experiment submit -n {name} -d {minutes} "
                         f"-l {nodes},archi=m3:at86rf231+site={self.site}")
        exp = json.loads(reply)["id"]
        self.run(f"iotlab-experiment wait -i {exp}")
        return exp

    def flash(self, exp, elf):
        subprocess.run(["scp", "-q", elf, f"{self.host}:prasle.elf"], check=True)
        reply = json.loads(self.run(f"iotlab-node --flash prasle.elf -i {exp}"))
        return sorted((address.split(".")[0] for address in reply.get("0", [])),
                      key=board_number)

    def release(self, exp):
        self.run(f"iotlab-experiment stop -i {exp}")


def board_number(board):
    """m3-157 to 157, which is also the node id"""
    return int(board.split("-")[1])


class Serial:
    """
    Aggregator prints <time>;<board>;<line> for each line a node writes.
    sends <board>;<command> to that board or -;<command> to all of them
    """

    def __init__(self, testbed, exp):
        self.process = subprocess.Popen(["ssh", testbed.host, f"serial_aggregator -i {exp}"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        time.sleep(3)

    def _read(self):
        for line in self.process.stdout:
            parts = line.rstrip("\n").split(";", 2)
            if len(parts) == 3:
                self.lines.put((parts[1], parts[2].strip()))

    def send(self, board, command):
        self.process.stdin.write(f"{board or '-'};{command}\n")
        self.process.stdin.flush()

    def send_to_all(self, command):
        self.send(None, command)

    def wait_for(self, boards, keyword, timeout):
        answers = {}
        deadline = time.time() + timeout
        while len(answers) < len(boards):
            left = deadline - time.time()
            if left <= 0:
                break
            try:
                board, text = self.lines.get(timeout=left)
            except queue.Empty:
                break
            if board in boards and board not in answers and text.split(" ", 1)[0] == keyword:
                answers[board] = text
        return answers

    def ask(self, commands, keyword, expected=None, attempts=3, timeout=3):
        missing = dict(commands)
        answers = {}
        for _ in range(attempts):
            for board, command in missing.items():
                self.send(board, command)
                # Back to back writes can overrun the aggregator on large fleets.
                time.sleep(0.02)
            for board, text in self.wait_for(set(missing), keyword, timeout).items():
                if expected is None or text == expected[board]:
                    answers[board] = text
                    del missing[board]
            if not missing:
                return answers
        raise RuntimeError(f"no '{keyword}' answer from {', '.join(missing)}")

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
