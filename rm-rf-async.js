#!/usr/bin/env gjs
// Recursively delete a directory using Gio
//
// Copyright (C) 2012 Colin Walters <walters@verbum.org>
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.

const Lang = imports.lang;
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;

var loop = GLib.MainLoop.new(null, true);

var numFilesDeleted = 0;
var numDirsDeleted = 0;

AsyncRmContext.prototype = {
    _init: function(path, callback) {
	this.numFilesPending = 0;
	this.path = path;
	this.callback = callback;
	this.haveMoreFiles = false;
    },

    onEndDirectoryDeleted: function(file, result) {
	file.delete_finish(result);
	this.callback();
    },

    checkNoChildren: function() {
	if (this.numFilesPending == 0 && !this.haveMoreFiles) {
	    this.path.delete_async(0, null, Lang.bind(this, this.onEndDirectoryDeleted));
	}
    },

    onFileDeleted: function(file, result) {
	file.delete_finish(result);
	numFilesDeleted++;
	this.numFilesPending--;
	this.checkNoChildren();
    },

    onRmRfComplete: function() {
	numDirsDeleted++;
	this.numFilesPending--;
	this.checkNoChildren();
    },

    onNextFiles: function(enumerator, result) {
	var parent = enumerator.get_container();
	let files = enumerator.next_files_finish(result);
	let haveMoreFiles = files.length > 0;
	
	if (haveMoreFiles) {
	    this.haveMoreFiles = true;
	    enumerator.next_files_async(20, 0, null,
					Lang.bind(this, this.onNextFiles));
	} else {
	    this.haveMoreFiles = false;
	    enumerator.close(null);
	    this.checkNoChildren();
	}

	for (let i = 0; i < files.length; i++) {
	    let fileInfo = files[i];
	    let file = parent.get_child(fileInfo.get_name());
	    this.numFilesPending++;
	    if (fileInfo.get_file_type() == Gio.FileType.DIRECTORY) {
		rmRfAsync(file, Lang.bind(this, this.onRmRfComplete));
	    } else { 
		file.delete_async(0, null, Lang.bind(this, this.onFileDeleted));
	    }
	}
    },

    onEnumerateChildren: function(path, result) {
	let enumerator = path.enumerate_children_finish(result);
	enumerator.next_files_async(20, 0, null,
				    Lang.bind(this, this.onNextFiles));
    }
};

function AsyncRmContext(path, context) {
    this._init(path, context);
}

function rmRfAsync(path, callback) {
    var context = new AsyncRmContext(path, callback);
    path.enumerate_children_async("standard::name,standard::type",
				  Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
				  0, null, Lang.bind(context, context.onEnumerateChildren));
				  
};

// Main code

var path = Gio.file_new_for_path(ARGV[0]);

rmRfAsync(path, function () { loop.quit(); });

loop.run();
