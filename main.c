/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "cmaze.h"

int main(int argc, char **argv)
{
	int err = 0;
	int num_rows = 121;
	int num_cols = 121;
	gboolean difficult = FALSE;
	uint anim_speed = 100;
	int seed = 0;
	struct Maze *maze;

	GError *error = NULL;
	GOptionContext *context;
	GOptionEntry entries[] = {
		{ "num-rows",   'r', 0, G_OPTION_ARG_INT, &num_rows,
		  "Number of rows", "ROWS" },
		{ "num-cols",   'c', 0, G_OPTION_ARG_INT, &num_cols,
		  "Number of columns", "COLS" },
		{ "difficult",  'd', 0, G_OPTION_ARG_NONE, &difficult,
		  "Produce a more complex maze", NULL },
		{ "anim-speed", 'a', 0, G_OPTION_ARG_INT, &anim_speed,
		  "Specify the animation speed (in percent)", "VAL" },
		{ "rand-seed",  's', 0, G_OPTION_ARG_INT, &seed,
		  "Random seed value", "VAL" },
		{ NULL }
	};

	context = g_option_context_new("- Maze generator and solver");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_fprintf(stderr, "option parsing failed: %s\n", error->message);
		return -1;
	}

	if (!seed)
		seed = time(NULL);
	srand(seed);

	maze = maze_alloc();
	maze_set_solver_algorithm(maze, SOLVER_A_STAR);
	maze_set_anim_speed(maze, anim_speed);

	err = maze_create(maze, num_rows, num_cols, difficult);
	if (err) {
		g_fprintf(stderr, "create_maze failed\n");
		goto exit_err;
	}

	err = gtk_maze_run(maze);

exit_err:
	maze_free(maze);

	return err;
}
