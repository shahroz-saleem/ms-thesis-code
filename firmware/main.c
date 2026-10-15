#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

#include "prasle.h"
#include "radio.h"

static int cmd_id(int argc, char **argv)
{
    if (argc != 2) {
        puts("usage: id <node id>");
        return 1;
    }

    unsigned long id = strtoul(argv[1], NULL, 10);

    if (id == 0 || id >= UINT16_MAX || radio_set_id(id) < 0) {
        puts("id failed");
        return 1;
    }
    printf("id %lu\n", id);
    return 0;
}

static int cmd_probe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    radio_probe();
    return 0;
}

static int cmd_heard(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    radio_print_heard();
    return 0;
}

static int cmd_setup(int argc, char **argv)
{
    uint16_t neighbors[PRASLE_MAX_NEIGHBORS];
    unsigned count = 0;

    if (argc != 5) {
        puts("usage: setup <rank> <K> <T ms> <neighbor,neighbor,...>");
        return 1;
    }

    unsigned rank = strtoul(argv[1], NULL, 10);
    unsigned k_rounds = strtoul(argv[2], NULL, 10);
    unsigned round_ms = strtoul(argv[3], NULL, 10);

    for (char *id = strtok(argv[4], ","); id && count < PRASLE_MAX_NEIGHBORS;
         id = strtok(NULL, ",")) {
        neighbors[count++] = strtoul(id, NULL, 10);
    }
    if (prasle_setup(rank, k_rounds, round_ms, neighbors, count) < 0) {
        puts("setup failed");
        return 1;
    }

    printf("setup rank=%u k=%u t=%u neighbors=", rank, k_rounds, round_ms);
    for (unsigned i = 0; i < count; i++) {
        printf("%s%u", i ? "," : "", neighbors[i]);
    }
    printf("\n");
    return 0;
}

static int cmd_start(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    prasle_start();
    return 0;
}

SHELL_COMMAND(id, "set this node's id: id <n>", cmd_id);
SHELL_COMMAND(probe, "broadcast a few probe frames", cmd_probe);
SHELL_COMMAND(heard, "print the average RSSI of every node heard", cmd_heard);
SHELL_COMMAND(setup, "setup <rank> <K> <T ms> <neighbor,neighbor,...>", cmd_setup);
SHELL_COMMAND(start, "start the election", cmd_start);

int main(void)
{
    char line[SHELL_DEFAULT_BUFSIZE];

    prasle_init();
    shell_run(NULL, line, sizeof(line));
    return 0;
}
