import unittest
import json
import subprocess
import ddt
import redis

tests = {
    "simple_ping_test": [["PING"] for i in range(0, 10000)],
    "simple_incr_test": [["INCR", "test"] for i in range(0, 10000)],
    "big_data_read_write": [
        ["SET", "test", "super_long_string" * 3500000],
        ["GET", "test"],
    ],
}

raw_tests = ("",)


def generate_test_files():
    for name, data in tests.items():
        if name in raw_tests:
            with open("%s.data" % name, "w") as f:
                for cmd in data:
                    f.write(" ".join(cmd))
                    f.write("--data-separator--")
        else:
            with open("%s.json" % name, "w") as f:
                f.write(json.dumps(data))


@ddt.ddt
class TestStringMethods(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        generate_test_files()

    @ddt.data(*tests.keys())
    def test_runner(self, test_file):
        print(test_file)
        return_code = subprocess.call(
            ["bin/qredis-runner", "%s.json" % test_file]
        )

        self.assertEqual(return_code, 0)


if __name__ == '__main__':
    unittest.main()
