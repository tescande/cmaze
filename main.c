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

	err = maze_create(maze, num_rows, num_cols, true);
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
