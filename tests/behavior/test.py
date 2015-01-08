#!/usr/bin/env python3

import os.path
import re
import sys
import tempfile
import subprocess

VERBOSE = 0

def run(exe, args):
    sub = subprocess.Popen(executable = exe, args = args,
            stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = sub.communicate()
    retcode = sub.wait()
    return (stdout, stderr, retcode)

class TestCmd(object):
    # command types
    PORT = 1   # define a port on the DUT
    CYCLE = 2  # advance to a given cycle
    WRITE = 3  # write an input to the DUT
    EXPECT = 4 # expect a given value on a given output from the DUT

    num = '((\d+)|(0x[0-9a-fA-F]+)|(0b[01]+))'
    port_re = re.compile('^port (\w+) (\d+)$')
    cycle_re = re.compile('^cycle (\d+)$')
    write_re = re.compile('^write (\w+)\s* \s*' + num + '$')
    expect_re = re.compile('^expect (\w+)\s* \s*' + num + '$')

    def __init__(self, text):
        self.text = text
        self.cmdtype = 0
        self.cycle = 0
        self.port = 0
        self.data = 0
        self.width = 0

        if not self.parse():
            raise Exception("Could not parse text: " + text)

    def __str__(self):
        type_str = '(none)'
        if self.cmdtype == TestCmd.PORT: type_str = "PORT"
        elif self.cmdtype == TestCmd.CYCLE: type_str = "CYCLE"
        elif self.cmdtype == TestCmd.WRITE: type_str = "WRITE"
        elif self.cmdtype == TestCmd.EXPECT: type_str = "EXPECT"
        return ("TestCmd(type=%s,cycle=%d,port=%s,data=%d,width=%d)" %
            (type_str, self.cycle, self.port, self.data, self.width))

    def parse_num(self, t):
        if t.startswith('0x'):
            return int(t[2:], 16)
        elif t.startswith('0b'):
            return int(t[2:], 2)
        else:
            return int(t)

    def parse(self):
        self.text = self.text.strip()
        m = TestCmd.port_re.match(self.text)
        if m is not None:
            g = m.groups()
            self.cmdtype = TestCmd.PORT
            self.port = g[0]
            self.width = int(g[1])
            return True

        m = TestCmd.cycle_re.match(self.text)
        if m is not None:
            g = m.groups()
            self.cmdtype = TestCmd.CYCLE
            self.cycle = int(g[0])
            return True

        m = TestCmd.write_re.match(self.text)
        if m is not None:
            g = m.groups()
            self.cmdtype = TestCmd.WRITE
            self.port = g[0]
            self.data = self.parse_num(g[1])
            return True

        m = TestCmd.expect_re.match(self.text)
        if m is not None:
            g = m.groups()
            self.cmdtype = TestCmd.EXPECT
            self.port = g[0]
            self.data = self.parse_num(g[1])
            return True

class TestCase(object):
    def __init__(self, filename):
        self.filename = filename
        self.testcmds = []

    def load(self):
        with open(self.filename) as of:
            for line in of.readlines():
                if line.startswith('#test:'):
                    self.testcmds.append(TestCmd(line.strip()[6:]))

    def write_tb(self, out_filename):
        with open(out_filename, 'w') as of:
            of.write("module tb;\n\n")
            of.write("reg clock;\nreg reset;\ninitial clock = 0;\ninitial reset = 0;\n\n")
            of.write("reg [63:0] cycle_counter;\ninitial cycle_counter = 0;\n")
            of.write("always begin #5; clock = 1; cycle_counter = cycle_counter + 1; #5; clock = 0; end\n\n")

            of.write("main dut(.clock(clock), .reset(reset)")

            portwidths = []
            portwidth_map = {}
            port_written = {}
            for c in self.testcmds:
                if c.cmdtype == TestCmd.PORT:
                    of.write(",\n.%s(%s)" % (c.port, c.port))
                    portwidths.append( (c.port, c.width) )
                    portwidth_map[c.port] = c.width
                    port_written[c.port] = False
                if c.cmdtype == TestCmd.WRITE:
                    port_written[c.port] = True
            of.write(");\n\n")

            for (port, width) in portwidths:
                if port_written[port]:
                    of.write("reg [%d:0] %s;\n" % (width - 1, port))
                else:
                    of.write("wire [%d:0] %s;\n" % (width - 1, port))
            of.write("\n")

            if VERBOSE:
                of.write("always @(negedge clock) begin\n")
                of.write("$display(\"\\n====== cycle %d: ======\\n\", cycle_counter);\n")
                for (port, width) in portwidths:
                    of.write("$display(\"* %s = %%d\", %s);\n" % (port, port))
                of.write("end\n")

            cur_cycle = 0
            of.write("initial begin\n")
            for (port, width) in portwidths:
                if port_written[port]:
                    of.write("    %s = %d'd0;\n" % (port, width))
            of.write("    reset = 1; #5; reset = 0; #5;\n")
            for c in self.testcmds:
                if c.cmdtype == TestCmd.CYCLE:
                    if c.cycle < cur_cycle:
                        print("Warning: trying to reverse time (cycle %d)" % c.cycle)
                        continue
                    of.write("    #%d;\n" % ((c.cycle - cur_cycle) * 10))
                    cur_cycle = c.cycle
                if c.cmdtype == TestCmd.WRITE:
                    of.write("    %s = %d'd%d;\n" % (c.port, portwidth_map[c.port], c.data))
                if c.cmdtype == TestCmd.EXPECT:
                    of.write("    if (%s != %d'd%d) begin\n" % (c.port, portwidth_map[c.port], c.data))
                    of.write("        $display(\"Data mismatch (cycle %%d): port %s should be %d but is %%d.\", cycle_counter, %s);\n" %
                                (c.port, c.data, c.port))
                    of.write("        $display(\"FAILED.\");\n")
                    of.write("        $finish;\n")
                    of.write("    end\n")
            of.write("    #10;\n")
            of.write("    $display(\"PASSED.\");\n")
            of.write("    $finish;\n")
            of.write("end\n\n")

            of.write("endmodule\n")

    def run(self, autopiper_bin):
        tmppath = tempfile.mkdtemp()

        exe = tmppath + os.path.sep + os.path.basename(self.filename) + '_test'
        dut_v = tmppath + os.path.sep + os.path.basename(self.filename) + '_dut.v'
        tb_v = tmppath + os.path.sep + os.path.basename(self.filename) + '_tb.v'

        stdout, stderr, ret = run(autopiper_bin, [autopiper_bin, '-o', dut_v, self.filename])
        if ret != 0:
            print("Error compiling DUT:")
            print(stderr.decode('utf-8'))
            return False

        self.write_tb(tb_v)

        stdout, stderr, ret = run("iverilog", ["iverilog", '-o', exe, dut_v, tb_v])
        if ret != 0:
            print("Error compiling DUT and testbench Verilog to test executable:")
            print(stderr.decode('utf-8'))
            return False

        stdout, stderr, ret = run(exe, [exe])
        if ret != 0:
            print("Error running test.")
            print(stderr.decode('utf-8'))
            return False
        if not stdout.endswith(b'PASSED.\n'):
            print("Test failed:")
            print(stdout.decode('utf-8'))
            return False

        os.system('rm -rf ' + tmppath)
        return True

t = TestCase(sys.argv[2])
t.load()
if t.run(sys.argv[1]):
    sys.exit(0)
else:
    sys.exit(1)
