/* SPDX-License-Identifier: MIT */
#include <gtk/gtk.h>

#include "cmaze.h"

struct MazeGui {
	struct Maze *maze;

	GtkApplication *app;
	GtkWidget *drawing_area;
	GtkLabel  *info_label;
	GtkSpinButton  *spin_num_rows;
	GtkSpinButton  *spin_num_cols;
	GtkWidget *new_button;
	GtkWidget *clear_button;
	GtkWidget *solve_button;
	GtkToggleButton *difficult_check;
	GtkComboBoxText *algo_combo;

	int cell_width;
	int cell_height;
	cairo_surface_t *surface;
	cairo_t *cr;
};

typedef enum {
	BLACK = 0,
	WHITE,
	RED,
	GREEN,
	BLUE,
	LIGHTBLUE,
	LIGHTGRAY,
	DARKGRAY,
} CellColor;

static void get_gdk_color(CellColor cell_color, GdkRGBA *color)
{
	color->alpha = 1.0;

	switch (cell_color) {
	case BLACK:
		color->red =
		color->green =
		color->blue = 0;
		break;
	case WHITE:
		color->red =
		color->green =
		color->blue = 1.0;
		break;
	case RED:
		color->red = 1.0;
		color->green =
		color->blue = 0;
		break;
	case GREEN:
		color->green = 1.0;
		color->red =
		color->blue = 0;
		break;
	case BLUE:
		color->red =
		color->green = 0.0;
		color->blue = 1.0;
		break;
	case LIGHTBLUE:
		color->red = 0.0;
		color->green = 1.0;
		color->blue = 1.0;
		break;
	case LIGHTGRAY:
		color->green =
		color->red =
		color->blue = 0.8;
		break;
	case DARKGRAY:
		color->green =
		color->red =
		color->blue = 0.5;
		break;
	}
}

static void label_set_text(GtkLabel *label, char *format, ...)
{
	char *buf = NULL;
	int size;
	va_list ap;

	if (!format)
		goto exit_err;

	va_start(ap, format);
	size = g_vsnprintf(NULL, 0, format, ap);
	va_end(ap);
	if (size < 0)
		goto exit_err;

	size++;
	buf = g_malloc0(size);

	va_start(ap, format);
	size = g_vsnprintf(buf, size, format, ap);
	va_end(ap);
	if (size < 0)
		goto exit_err;

	gtk_label_set_text(label, buf);
	g_free(buf);

	return;

exit_err:
	g_free(buf);

	g_fprintf(stderr, "Can't set text label\n");
}

static void cairo_surface_free(struct MazeGui *gui)
{
	cairo_destroy(gui->cr);
	cairo_surface_destroy(gui->surface);
}

static void cairo_surface_alloc(struct MazeGui *gui)
{
	GtkAllocation rect = { 0 };
	GtkAllocation curr_rect;
	GdkDisplay *disp;
	GdkMonitor *monitor;
	int num_monitors;
	int num_rows;
	int num_cols;
	int surface_width;
	int surface_height;

	num_rows = maze_get_num_rows(gui->maze);
	num_cols = maze_get_num_cols(gui->maze);

	/* Get the largest screen resolution for the maze cairo surface */
	disp = gdk_display_get_default();
	num_monitors = gdk_display_get_n_monitors(disp);
	while (num_monitors--) {
		monitor = gdk_display_get_monitor(disp, num_monitors);
		gdk_monitor_get_workarea(monitor, &curr_rect);
		if (curr_rect.width * curr_rect.height > rect.width * rect.height)
			rect = curr_rect;
	}

	gui->cell_width = (rect.width / num_cols) + 1;
	gui->cell_height = (rect.height / num_rows) + 1;

	surface_width = gui->cell_width * num_cols;
	surface_height = gui->cell_height * num_rows;

	gui->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						  surface_width,
						  surface_height);
	gui->cr = cairo_create(gui->surface);
}

static void on_new_clicked(GtkButton *button, struct MazeGui *gui)
{
	int num_rows;
	int num_cols;
	gboolean difficult;

	num_rows = gtk_spin_button_get_value(gui->spin_num_rows);
	num_cols = gtk_spin_button_get_value(gui->spin_num_cols);
	difficult = gtk_toggle_button_get_active(gui->difficult_check);

	maze_create(gui->maze, num_rows, num_cols, difficult);

	gtk_spin_button_set_value(gui->spin_num_rows, maze_get_num_rows(gui->maze));
	gtk_spin_button_set_value(gui->spin_num_cols, maze_get_num_cols(gui->maze));

	cairo_surface_free(gui);
	cairo_surface_alloc(gui);

	gtk_widget_queue_draw(gui->drawing_area);
}

static void on_clear_clicked(GtkButton *button, struct MazeGui *gui)
{
	maze_clear_board(gui->maze);

	gtk_widget_queue_draw(gui->drawing_area);
}

static void maze_solver_cb(int reason, struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;

	if (reason == SOLVER_CB_REASON_SOLVED ||
	    reason == SOLVER_CB_REASON_CANCELED) {
		gtk_widget_set_sensitive(gui->new_button, TRUE);
		gtk_widget_set_sensitive(gui->clear_button, TRUE);
		gtk_button_set_label(GTK_BUTTON(gui->solve_button), "Solve");

		if (reason == SOLVER_CB_REASON_SOLVED)
			label_set_text(gui->info_label, "Length: %d\nTime: %.03fs",
				       maze_get_path_length(maze),
				       maze_get_solve_time(maze));
	}

	gtk_widget_queue_draw(gui->drawing_area);
}

static void on_solve_clicked(GtkButton *button, struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;
	SolverAlgorithm algo;

	if (maze_solver_running(maze)) {
		maze_solve_thread_cancel(maze);
		return;
	}

	algo = gtk_combo_box_get_active(GTK_COMBO_BOX(gui->algo_combo));
	maze_set_solver_algorithm(maze, algo);

	gtk_widget_set_sensitive(gui->new_button, FALSE);
	gtk_widget_set_sensitive(gui->clear_button, FALSE);
	gtk_button_set_label(GTK_BUTTON(gui->solve_button), "Cancel");
	label_set_text(gui->info_label, "");

	maze_solve_thread(maze, (MazeSolverFunc)maze_solver_cb, gui);
}

static void on_draw(GtkDrawingArea *da, cairo_t *cr, struct MazeGui *gui)
{
	GtkAllocation da_rect;
	int cell_width;
	int cell_height;
	int row, col;
	CellColor cell_color;
	GdkRGBA color;
	struct Maze *maze = gui->maze;
	int num_rows;
	int num_cols;
	int surface_width;
	int surface_height;
	double scale_x;
	double scale_y;

	gtk_widget_get_allocated_size(GTK_WIDGET(da), &da_rect, NULL);

	num_rows = maze_get_num_rows(maze);
	num_cols = maze_get_num_cols(maze);

	cell_width = gui->cell_width;
	cell_height = gui->cell_height;

	cairo_set_source_rgba(gui->cr, 1.0, 1.0, 1.0, 1.0);
	cairo_paint(gui->cr);

	for (row = 0; row < num_rows; row++) {
		for (col = 0; col < num_cols; col++) {
			GtkAllocation rect;
			CellType cell_type;

			cell_type = maze_get_cell_type(maze, row, col);

			switch (cell_type) {
			case CELL_TYPE_EMPTY:
				continue;

			case CELL_TYPE_WALL:
				cell_color = BLACK;
				break;
			case CELL_TYPE_START:
				cell_color = RED;
				break;
			case CELL_TYPE_END:
				cell_color = LIGHTBLUE;
				break;
			case CELL_TYPE_PATH_HEAD:
				cell_color = DARKGRAY;
				break;
			case CELL_TYPE_PATH_VISITED:
				cell_color = LIGHTGRAY;
				break;
			case CELL_TYPE_PATH_SOLUTION:
				cell_color = GREEN;
				break;
			}

			get_gdk_color(cell_color, &color);

			rect.x = col * cell_width;
			rect.y = row * cell_height;
			rect.width = cell_width;
			rect.height = cell_height;

			gdk_cairo_set_source_rgba(gui->cr, &color);
			gdk_cairo_rectangle(gui->cr, &rect);

			cairo_fill(gui->cr);
		}
	}

	surface_width = cairo_image_surface_get_width(gui->surface);
	surface_height = cairo_image_surface_get_height(gui->surface);

	scale_x = (double)da_rect.width / surface_width;
	scale_y = (double)da_rect.height / surface_height;

	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, gui->surface, 0.0, 0.0);
	cairo_paint(cr);
}

static void on_speed_changed(GtkRange *range, struct MazeGui *gui)
{
	maze_set_anim_speed(gui->maze, (uint)gtk_range_get_value(range));
}

static gboolean on_mouse_clicked(GtkWidget *da, GdkEventButton *event,
				 struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;
	GtkAllocation rect;
	int row;
	int col;

	gtk_widget_get_allocated_size(da, &rect, NULL);
	row = event->y * maze_get_num_rows(maze) / rect.height;
	col =  event->x * maze_get_num_cols(maze) / rect.width;

	if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		maze_set_end_cell(maze, row, col);
	else
		maze_set_start_cell(maze, row, col);

	gtk_widget_queue_draw(da);

	return TRUE;
}

static void on_destroy(GtkWindow *win, struct MazeGui *gui)
{
	maze_solve_thread_cancel(gui->maze);
}

static void gui_activate(GtkApplication* app, struct MazeGui *gui)
{
	struct Maze *maze = gui->maze;
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *vbox2;
	GtkWidget *drawing_area;
	GtkWidget *label_rows;
	GtkWidget *label_cols;
	GtkWidget *spin;
	GtkToggleButton *check;
	GtkWidget *button;
	GtkWidget *label;
	GtkComboBoxText *combo;
	GtkWidget *scale;
	GtkWidget *frame;

	cairo_surface_alloc(gui);

	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "CMaze");
	g_signal_connect(G_OBJECT(window), "destroy",
			 G_CALLBACK(on_destroy), gui);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	gtk_box_set_spacing(GTK_BOX(hbox), 5);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_set_spacing(GTK_BOX(vbox), 5);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

	drawing_area = gtk_drawing_area_new();
	gui->drawing_area = g_object_ref(drawing_area);
	gtk_widget_set_size_request(drawing_area, 500, 500);
	gtk_box_pack_start(GTK_BOX(hbox), drawing_area, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(drawing_area), "draw",
			 G_CALLBACK(on_draw), gui);
	gtk_widget_add_events(drawing_area,
			      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(G_OBJECT(drawing_area), "button-release-event",
			 G_CALLBACK(on_mouse_clicked), gui);

	frame = gtk_frame_new("Maze");
	gtk_frame_set_label_align(GTK_FRAME(frame), 0.05, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

	vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), 10);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

	label_rows = gtk_label_new("Rows:");
	gtk_label_set_xalign(GTK_LABEL(label_rows), 1.0);
	gtk_box_pack_start(GTK_BOX(hbox), label_rows, TRUE, FALSE, 0);

	spin = gtk_spin_button_new_with_range(MAZE_MIN_ROWS, MAZE_MAX_ROWS, 10);
	gui->spin_num_rows = GTK_SPIN_BUTTON(spin);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), maze_get_num_rows(maze));
	gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), hbox, FALSE, FALSE, 0);

	label_cols = gtk_label_new("Cols:");
	gtk_label_set_xalign(GTK_LABEL(label_cols), 1.0);
	gtk_box_pack_start(GTK_BOX(hbox), label_cols, TRUE, FALSE, 0);

	spin = gtk_spin_button_new_with_range(MAZE_MIN_COLS, MAZE_MAX_COLS, 10);
	gui->spin_num_cols = GTK_SPIN_BUTTON(spin);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), maze_get_num_cols(maze));
	gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);

	check = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label("Difficult"));
	gui->difficult_check = check;
	gtk_toggle_button_set_active(check, maze_get_difficult(maze));
	gtk_box_pack_start(GTK_BOX(vbox2), GTK_WIDGET(check), FALSE, FALSE, 0);

	button = gtk_button_new_with_label("New");
	gui->new_button = g_object_ref(button);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_new_clicked), gui);
	gtk_box_pack_start(GTK_BOX(vbox2), button, FALSE, FALSE, 0);

	button = gtk_button_new_with_label("Clear");
	gui->clear_button = g_object_ref(button);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_clear_clicked), gui);
	gtk_box_pack_start(GTK_BOX(vbox2), button, FALSE, FALSE, 0);

	frame = gtk_frame_new("Solver Algorithm");
	gtk_frame_set_label_align(GTK_FRAME(frame), 0.1, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), hbox);

	combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
	gui->algo_combo = combo;
	gtk_combo_box_text_insert_text(combo, SOLVER_BFS, "Breadth-First Search");
	gtk_combo_box_text_insert_text(combo, SOLVER_DFS, "Depth-First Search");
	gtk_combo_box_text_insert_text(combo, SOLVER_A_STAR, "A Star");
	gtk_combo_box_text_insert_text(combo, SOLVER_ALWAYS_TURN_LEFT, "Always Turn Left");
	gtk_combo_box_text_insert_text(combo, SOLVER_ALWAYS_TURN_RIGHT, "Always Turn Right");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo),
				 maze_get_solver_algorithm(maze));
	gtk_container_add(GTK_CONTAINER(hbox), GTK_WIDGET(combo));

	frame = gtk_frame_new("Animation Speed");
	gtk_frame_set_label_align(GTK_FRAME(frame), 0.1, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), hbox);

	scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 50, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_range_set_value(GTK_RANGE(scale), maze_get_anim_speed(maze));
	g_signal_connect(G_OBJECT(scale), "value-changed",
			 G_CALLBACK(on_speed_changed), gui);
	gtk_container_add(GTK_CONTAINER(hbox), scale);

	button = gtk_button_new_with_label("Solve");
	gui->solve_button = g_object_ref(button);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(on_solve_clicked), gui);
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 3);

	label = gtk_label_new("");
	gui->info_label = GTK_LABEL(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
}

static void gui_cleanup(struct MazeGui *gui)
{
	g_object_unref(gui->drawing_area);
	g_object_unref(gui->new_button);
	g_object_unref(gui->clear_button);
	g_object_unref(gui->solve_button);

	cairo_surface_free(gui);
}

int gtk_maze_run(struct Maze *maze)
{
	struct MazeGui *gui;

	gui = g_malloc0(sizeof(*gui));
	gui->maze = maze;

	gui->app = gtk_application_new("org.escande.boids", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(gui->app, "activate", G_CALLBACK(gui_activate), gui);

	g_application_run(G_APPLICATION(gui->app), 0, NULL);

	g_object_unref(gui->app);

	gui_cleanup(gui);
	g_free(gui);

	return 0;
}
