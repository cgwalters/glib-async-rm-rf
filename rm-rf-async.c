/*
 * Recursively delete a directory using Gio
 * Copyright (C) 2012 Colin Walters <walters@verbum.org>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gio/gio.h>

#include <unistd.h>
#include <stdlib.h>

typedef struct {
  gint priority;
  GCancellable *cancellable;
  guint num_files_pending;
  gboolean have_more_files;
  GFile *file;
  GSimpleAsyncResult *result;
} AsyncRmContext;

static guint files_deleted = 0;

static void fatal_gerror (GError *error) G_GNUC_NORETURN;

static void
rm_rf_async (GFile               *file,
	     gint                 io_priority,
	     GCancellable        *cancellable,
	     GAsyncReadyCallback  callback,
	     gpointer             user_data);

static gboolean
rm_rf_finish (GFile               *file,
	      GAsyncResult        *result,
	      GError             **error);

static void
fatal_gerror (GError *error)
{
  g_printerr ("%s\n", error->message);
  exit (1);
}

static void
on_end_directory_deleted (GObject         *file,
			  GAsyncResult    *result,
			  gpointer         user_data)
{
  AsyncRmContext *data = user_data;
  GError *local_error = NULL;

  if (!g_file_delete_finish ((GFile*)file, result, &local_error))
    fatal_gerror (local_error);

  files_deleted++;

  g_simple_async_result_complete (data->result);
  g_object_unref (data->result);
  g_object_unref (data->file);
  g_free (data);
}

static void
check_no_children (AsyncRmContext *data)
{
  if (data->num_files_pending == 0 && !data->have_more_files)
    g_file_delete_async (data->file, data->priority, data->cancellable,
			 on_end_directory_deleted, data);
}

static void
on_file_deleted (GObject        *object,
		 GAsyncResult   *result,
		 gpointer        user_data)
{
  AsyncRmContext *data = user_data;
  GError *local_error = NULL;

  if (!g_file_delete_finish ((GFile*)object, result, &local_error))
    fatal_gerror (local_error);

  files_deleted++;

  data->num_files_pending--;
  check_no_children (data);
}

static void
on_rm_rf_complete (GObject      *object,
		   GAsyncResult *result,
		   gpointer      user_data)
{
  AsyncRmContext *data = user_data;
  GError *local_error = NULL;
  
  if (!rm_rf_finish ((GFile*)object, result, &local_error))
    fatal_gerror (local_error);

  data->num_files_pending--;
  check_no_children (data);
}

static void
on_next_files (GObject      *object,
	       GAsyncResult *result,
	       gpointer      user_data)
{
  AsyncRmContext *data = user_data;
  GFileEnumerator *enumerator = (GFileEnumerator*)object;
  GError *local_error = NULL;
  GFile *parent = NULL;
  GList *files, *iter;

  files = g_file_enumerator_next_files_finish (enumerator,
					       result, &local_error);
  if (local_error)
    fatal_gerror (local_error);
  
  if (files)
    {
      data->have_more_files = TRUE;
      g_file_enumerator_next_files_async (enumerator, 20, G_PRIORITY_DEFAULT, NULL,
					  on_next_files, data);
    }
  else
    {
      data->have_more_files = FALSE;
      /* NOTE: NOT ASYNC */
      g_file_enumerator_close (enumerator, NULL, NULL);
      check_no_children (data);
    }

  parent = g_file_enumerator_get_container (enumerator);

  for (iter = files; iter; iter = iter->next)
    {
      GFileInfo *file_info = iter->data;
      GFile *file = g_file_get_child (parent, g_file_info_get_name (file_info));
      
      data->num_files_pending++;
      if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY)
	{
	  rm_rf_async (file, data->priority, data->cancellable, on_rm_rf_complete, data);
	}
      else
	{
	  g_file_delete_async (file, data->priority, NULL,
			       on_file_deleted, data);
	}
    }
}

static void
on_enumerate_children (GObject      *object,
		       GAsyncResult *result,
		       gpointer      user_data)
{
  AsyncRmContext *data = user_data;
  GFileEnumerator *enumerator;
  GError *local_error = NULL;
  
  enumerator = g_file_enumerate_children_finish ((GFile*)object, result, &local_error);
  if (!enumerator)
    fatal_gerror (local_error);

  g_file_enumerator_next_files_async (enumerator, 20, data->priority, data->cancellable,
				      on_next_files, data);
}

static void
rm_rf_async (GFile               *file,
	     gint                 io_priority,
	     GCancellable        *cancellable,
	     GAsyncReadyCallback  callback,
	     gpointer             user_data)
{
  AsyncRmContext *data = g_new0 (AsyncRmContext, 1);

  data->result = g_simple_async_result_new ((GObject*)file, callback, user_data, rm_rf_async);
  data->file = g_object_ref (file);
  data->priority = io_priority;
  data->cancellable = cancellable;
  
  g_file_enumerate_children_async (data->file, "standard::name,standard::type",
				   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				   io_priority, cancellable,
				   on_enumerate_children, data);
}

static gboolean
rm_rf_finish (GFile                *file,
	      GAsyncResult         *result,
	      GError              **error)
{
  GSimpleAsyncResult *simple = (GSimpleAsyncResult*) result;

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;
  return TRUE;
}

static void
on_rm_finished (GObject          *file,
		GAsyncResult     *result,
		gpointer          user_data)
{
  GMainLoop *loop = user_data;
  GError *local_error = NULL;

  if (!rm_rf_finish ((GFile*) file, result, &local_error))
    fatal_gerror (local_error);

  g_main_loop_quit (loop);
}

static gboolean
print_progress (gpointer user_data)
{
  g_print ("%u files deleted\n", files_deleted);
  return FALSE;
}

int
main (int argc, char **argv)
{
  GFile *path;
  GMainLoop *loop;

  /* Note - must be done before any threads */
  setenv ("GIO_USE_VFS", "local", 1);

  g_type_init ();

  loop = g_main_loop_new (NULL, TRUE);

  path = g_file_new_for_path (argv[1]);

  rm_rf_async (path, G_PRIORITY_DEFAULT, NULL, on_rm_finished, loop);

  g_timeout_add_seconds (1, print_progress, NULL);

  g_main_loop_run (loop);

  print_progress (NULL);

  return 0;
}

