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

	maze = calloc(1, sizeof(*maze));
	if (!maze) {
		err = -ENOMEM;
		goto exit_err;
	}

	maze->num_rows = num_rows;
	maze->num_cols = num_cols;
	maze->board = calloc(num_rows * num_cols, sizeof(struct Cell));
	if (!maze->board) {
		err = -ENOMEM;
		goto exit_err;
	}

	err = create_maze(maze);
	if (err) {
		printf("create_maze failed\n");
		goto exit_err;
	}

	solve(maze);
	print_board(maze);

exit_err:
	if (maze->board)
		free(maze->board);

	if (maze)
		free(maze);

	return err;
}
