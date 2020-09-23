/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cmaze.h"

static void usage(char *argv0)
{
	fprintf(stderr, "Usage: %s [-h] [-r ROWS] [-c COLS] [-C] [-a] [-s]\n",
		basename(argv0));
	fprintf(stderr, "optional arguments:\n");
	fprintf(stderr, "  -h        show this help message and exit\n");
	fprintf(stderr, "  -r ROWS   Maze rows\n");
	fprintf(stderr, "  -c COLS   Maze columns\n");
	fprintf(stderr, "  -d        Produce a more complex maze\n");
	fprintf(stderr, "  -a SPEED  Specify the animation speed (in percent)\n");
	fprintf(stderr, "  -s VALUE  Random seed value\n");
}

int main(int argc, char **argv)
{
	int err = 0;
	int opt;
	int num_rows = 121;
	int num_cols = 121;
	bool difficult = false;
	uint anim_speed = 100;
	int seed = 0;
	struct Maze *maze;

	while ((opt = getopt(argc, argv, "r:c:da:s:h")) != -1) {
		switch (opt) {
		case 'r':
		   num_rows = atoi(optarg);
		   break;
		case 'c':
		   num_cols = atoi(optarg);
		   break;
		case 'd':
		   difficult = true;
		   break;
		case 'a':
		   anim_speed = (uint)atoi(optarg);
		   break;
		case 's':
		   seed = atoi(optarg);
		   break;
		default: /* '?' */
		   usage(argv[0]);
		   return -1;
		}
	}

	if (!seed)
		seed = time(NULL);
	srand(seed);

	maze = maze_alloc();
	if (!maze) {
		err = -ENOMEM;
		goto exit_err;
	}

	maze_set_solver_algorithm(maze, SOLVER_A_STAR);

	maze_set_anim_speed(maze, anim_speed);

	err = maze_create(maze, num_rows, num_cols, difficult);
	if (err) {
		printf("create_maze failed\n");
		goto exit_err;
	}

	err = gtk_maze_run(maze);

exit_err:
	if (maze)
		maze_free(maze);

	return err;
}
