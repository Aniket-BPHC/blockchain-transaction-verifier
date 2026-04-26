# How to run

Make sure all the testcase files are in the same directory as the helper
Name your solution file as `solution.c`

Run the following commands

```bash
gcc helper-program-release.c -lpthread -o helper
gcc solution.c -lpthread -o solution

./helper <TESTCASE_NUMBER>

```

For eg, to run the first test case, use

```bash
./helper 1
```
