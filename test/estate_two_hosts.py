#!/usr/bin/env python3
"""A file on another of the user's hosts, played by snurra as it arrives.

    python3 test/estate_two_hosts.py PATH_TO_FUZZNETD PATH_TO_SNURRA_TESTS [--loss P]

Two of one user's hosts over loopback, as fuzznet's own live_catalog.py
makes them: A the root, B joined with a grant. A puts a WAV and catalogues
it; B learns the name and the file's reference at its rounds and holds none
of the bytes. Then, both running, snurra's
`a_file_on_another_host_plays_as_it_arrives` plays the file from B's socket:
B fetches from A as the reader asks, and every frame must be the file's own.

The daemons are fuzznet's helpers' (fuzznet/tool, the vendored pin), which
put each in its own process group and see every one gone on the way out;
the whole is bounded by their deadline. Exit 0 on a pass, 1 on a failure.

`--loss P` runs both daemons dropping P per cent of what they send, against
a daemon built with fuzznet's faults (`make fuzznetd-faults` there), with
three times the rounds -- as fuzznet's own scenarios take loss (its sec
850). Then B fetches slowly enough that the reader catches up with it.
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "fuzznet", "tool"))
from live_rejoin import ROUND_DONE_RE, command, daemon, expect  # noqa: E402
from live_trim import ask, failed  # noqa: E402
import live_trim  # noqa: E402

live_trim.DEADLINE = time.monotonic() + 300.0
PREKEY_HOST = slice(4, 68)
ROUNDS = 6
SECONDS = 10
RATE = 48000


def song(path):
	"""Ten seconds of a ramp under a tone: every frame tells itself apart."""
	with wave.open(path, "wb") as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(RATE)
		frames = bytearray()
		for i in range(SECONDS * RATE):
			v = (i % 20000) - 10000 + int(4000 * math.sin(2 * math.pi * 440 * i / RATE))
			frames += struct.pack("<h", v)
		w.writeframes(bytes(frames))


def main(argv):
	global ROUNDS
	if len(argv) == 5 and argv[3] == "--loss" and argv[4].isdigit():
		# NO SEED GIVEN, as fuzznet's: each daemon seeds its loss from its pid.
		os.environ["FZN_FAULT_LOSS"] = argv[4]
		ROUNDS *= 3
		live_trim.DEADLINE = time.monotonic() + 900.0
		argv = argv[:3]
	if len(argv) != 3:
		print(__doc__.strip().splitlines()[2], file=sys.stderr)
		return 2

	class run:
		pass

	run.fuzznetd = os.path.abspath(argv[1])
	tests = os.path.abspath(argv[2])
	run.started = []
	base = 21900 + (os.getpid() % 50) * 4
	port = {"a": str(base), "b": str(base + 1)}
	with tempfile.TemporaryDirectory(prefix="snurra-2h.") as scratch, \
	     tempfile.TemporaryDirectory(prefix="s2h.", dir="/tmp") as sock_dir:
		run.scratch = scratch
		dirs = {k: os.path.join(scratch, k) for k in port}
		socks = {k: os.path.join(sock_dir, k) for k in port}
		args = {"a": ["--udp-port=" + port["a"]],
		        "b": ["--udp-port=" + port["b"], "--root-at", "127.0.0.1", port["a"]]}
		started = {}

		def turn(who, tag):
			if who in started:
				started.pop(who).stop()
			started[who] = daemon(run, tag, dirs[who], socks[who], extra=args[who])
			started[who].wait_for(ROUND_DONE_RE, "%s's round" % who.upper())

		def until(check, what):
			for rnd in range(ROUNDS):
				if check():
					return
				for who in ("a", "b"):
					turn(who, "%s-%s%d" % (who, what, rnd))
			if not check():
				raise failed("%s: not after %d rounds" % (what, ROUNDS))

		try:
			for who in port:
				os.mkdir(dirs[who])
			command(run, ["--fuzznet-dir=" + dirs["a"], "--new-root"], None)
			prekey = command(run, ["--fuzznet-dir=" + dirs["b"], "--prekey"], None)
			card = command(run, ["--fuzznet-dir=" + dirs["a"], "--pair=" + prekey, "--delegable"],
			               None)
			command(run, ["--fuzznet-dir=" + dirs["b"], "--accept=" + card, "--join"], None)
			for who in ("a", "b"):
				turn(who, who + "0")
				expect(socks[who], who, "set setting host net/address 127.0.0.1 " + port[who],
				       "ok")

			payload = os.path.join(scratch, "song.wav")
			song(payload)
			put = ask(socks["a"], "put file " + payload)
			if not put.startswith("ok "):
				raise failed("a: put file answered %r" % put)
			ref = put.split()[1]
			root = ref[:64]
			expect(socks["a"], "a", "add catalog %s name song" % root, "ok")
			# THE NAME, THEN THE REFERENCE, as live_catalog.py waits: with the
			# reference alone B answered a play with "not in the library".
			until(lambda: root in ask(socks["b"], "list catalog " + root), "name")
			until(lambda: ask(socks["b"], "get reference " + root).strip() == "ok " + ref,
			      "reference")
			print("estate-2h: B lists A's file and holds its reference, and none of it")

			env = dict(os.environ)
			env.update({"SNURRA_TEST_CLASS": "estate", "SNURRA_FUZZNET_SOCKET": socks["b"],
			            "SNURRA_ESTATE_ROOT": root, "SNURRA_ESTATE_FILE": payload,
			            "QT_QPA_PLATFORM": "offscreen"})
			left = max(10.0, live_trim.DEADLINE - time.monotonic())
			r = subprocess.run([tests, "a_file_on_another_host_plays_as_it_arrives"], env=env,
			                   timeout=left, capture_output=True, text=True)
			sys.stdout.write(r.stdout)
			sys.stderr.write(r.stderr)
			if r.returncode != 0 or "PASS   : estate_test::a_file_on_another_host" not in r.stdout:
				raise failed("snurra did not play A's file through B")
			print("estate-2h: pass")
			return 0
		except failed as e:
			print("estate-2h: FAIL: %s" % e, file=sys.stderr)
			return 1
		except subprocess.TimeoutExpired as e:
			print("estate-2h: FAIL: past its deadline: %s" % e, file=sys.stderr)
			return 1
		finally:
			for d in list(started.values()):
				d.stop()
			# EVERY DAEMON THIS RUN STARTED GONE, as fuzznet's scenarios
			# check: by its pid, while that pid is still this run's child.
			for pid in run.started:
				try:
					with open("/proc/%d/cmdline" % pid, "rb") as f:
						alive = run.fuzznetd.encode() in f.read()
					with open("/proc/%d/stat" % pid) as f:
						alive = alive and int(f.read().rsplit(")", 1)[1].split()[1]) == os.getpid()
				except OSError:
					alive = False
				if alive:
					print("estate-2h: FAIL: daemon %d outlived the run" % pid, file=sys.stderr)
					return 1


if __name__ == "__main__":
	sys.exit(main(sys.argv))
