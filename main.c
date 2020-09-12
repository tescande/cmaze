/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cmaze.h"

int main(int argc, char **argv)
{
	int err = 0;
	int num_rows = 121;
	int num_cols = 121;
	struct Maze *maze;

	srand(time(NULL));

	maze = maze_alloc();
	if (!maze) {
		err = -ENOMEM;
		goto exit_err;
	}

	err = maze_create(maze, num_rows, num_cols);
	if (err) {
		printf("create_maze failed\n");
		goto exit_err;
	}

	maze_solve(maze);
	maze_print_board(maze);

	printf("Path length: %i\nTime: %li.%03lis\n", maze->path_len,
	       maze->solve_time.tv_sec, maze->solve_time.tv_usec / 1000);

exit_err:
	if (maze)
		maze_free(maze);

	return err;
}
