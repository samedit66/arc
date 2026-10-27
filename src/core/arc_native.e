note
	description: "Explicit native commands and borrowed-value queries over libarchive; owns no archive resources."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_NATIVE

create {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS}
	make

feature {NONE} -- Initialization

	make
			-- Initialize result slots without acquiring native resources.
		do
			last_status := Archive_ok
			create system_error_slot.make ({PLATFORM}.integer_32_bytes)
			create system_cleanup_slot.make ({PLATFORM}.integer_32_bytes)
			last_descriptor := -1
		ensure
			no_pointer: last_pointer = default_pointer
		end

feature {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS} -- Native command results

	last_status: INTEGER
			-- Libarchive result of the latest status-producing native command.

	last_pointer: POINTER
			-- Result of the latest pointer-producing command; ownership depends on that command.

	last_descriptor: INTEGER
			-- Owned descriptor returned by the latest exclusive creation; -1 on failure.

	last_cleanup_code: INTEGER
			-- Separate close failure from the latest root, classification or identity operation; zero otherwise.

	last_is_output_source: BOOLEAN
			-- Did the latest source identity check match the owned output file?

	check_output_source (descriptor: INTEGER; path: POINTER)
			-- Compare an ordinary source with the owned output and retain system diagnostics.
		do
			last_is_output_source := c_is_output_source (descriptor, path, system_error_slot.item, system_cleanup_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
			last_cleanup_code := system_cleanup_slot.read_integer_32 (0)
		end

	last_source_type: INTEGER
			-- Latest physically inspected AE_IF* type; zero for an error or unsupported reparse point.

	classify_source (path: POINTER)
			-- Inspect one native path without following symlinks or junctions.
		do
			last_source_type := c_source_type (path, system_error_slot.item, system_cleanup_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
			last_cleanup_code := system_cleanup_slot.read_integer_32 (0)
		end

	last_system_code: INTEGER
			-- Captured system error: positive errno, negative Win32 code, or zero on success.

feature {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS} -- Native constants

	Archive_ok: INTEGER
			-- Successful libarchive status.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return ARCHIVE_OK;"
		end

	Archive_eof: INTEGER
			-- Normal end of entry or archive.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return ARCHIVE_EOF;"
		end

	Archive_failed: INTEGER
			-- Expected libarchive operation failure.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return ARCHIVE_FAILED;"
		ensure
			instance_free: class
		end

	Directory_type: INTEGER
			-- Libarchive directory entry type AE_IFDIR.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return AE_IFDIR;"
		ensure
			instance_free: class
		end

	Regular_type: INTEGER
			-- Libarchive ordinary-file entry type AE_IFREG.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return AE_IFREG;"
		ensure
			instance_free: class
		end

feature {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS} -- Fresh resource queries

	new_writer: POINTER
			-- Fresh owned writer; native allocation failure is fatal.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive *a = archive_write_new(); if (!a) abort(); return a;"
		ensure
			non_null_result: Result /= default_pointer
		end

	new_reader: POINTER
			-- Fresh owned reader; native allocation failure is fatal.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive *a = archive_read_new(); if (!a) abort(); return a;"
		ensure
			non_null_result: Result /= default_pointer
		end

	new_disk_writer: POINTER
			-- Fresh owned extraction writer; native allocation failure is fatal.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive *a = archive_write_disk_new(); if (!a) abort(); return a;"
		ensure
			non_null_result: Result /= default_pointer
		end

	new_entry: POINTER
			-- Fresh owned entry; native allocation failure is fatal.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive_entry *e = archive_entry_new(); if (!e) abort(); return e;"
		ensure
			non_null_result: Result /= default_pointer
		end

	cloned_entry (entry: POINTER): POINTER
			-- Independent owned copy of an entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive_entry *e = archive_entry_clone($entry); if (!e) abort(); return e;"
		ensure
			non_null_result: Result /= default_pointer
		end

feature {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS} -- Borrowed value queries

	entry_size (entry: POINTER): INTEGER_64
			-- Declared logical size of an entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_entry_size($entry);"
		end

	has_entry_size (entry: POINTER): BOOLEAN
			-- Does this entry declare a size?
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_entry_size_is_set($entry);"
		end

	entry_type (entry: POINTER): INTEGER
			-- Libarchive AE_IF* type of an entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_entry_filetype($entry);"
		end

	error_code (handle: POINTER): INTEGER
			-- Native errno-style diagnostic code retained by this handle.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int code = archive_errno($handle); if (code == ENOMEM) abort(); return code;"
		end

	error_message (handle: POINTER): POINTER
			-- Borrowed native diagnostic bytes, possibly NULL.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return (void *)archive_error_string($handle);"
		end

feature {ARC_WRITER, ARC_READER, ARC_ERROR, ARC_ARCHIVE, ARC_PATHS} -- Native commands

	entry_free (entry: POINTER)
			-- Release an owned entry, never a borrowed reader entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"archive_entry_free($entry);"
		end

	read_entry_name (reader, entry: POINTER)
			-- Convert the entry name and publish its borrowed UTF-8 pointer; copy before advancing.
		do
			last_pointer := c_entry_name (reader, entry)
			if last_pointer = default_pointer then
				last_status := Archive_failed
			else
				last_status := Archive_ok
			end
		end

	entry_set_name (entry, name: POINTER)
			-- Copy a UTF-8 archive pathname into an owned entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"archive_entry_set_pathname_utf8($entry, $name);"
		end

	entry_set_native_name (entry, name: POINTER)
			-- Copy a platform-native filesystem pathname into an owned entry.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_set_native_name($entry, $name);"
		end

	entry_set_directory (entry: POINTER)
			-- Configure an empty directory with permissions 0755.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"archive_entry_set_filetype($entry, AE_IFDIR); archive_entry_set_perm($entry, 0755); archive_entry_set_size($entry, 0);"
		end

	set_tar (handle: POINTER)
			-- Configure restricted PAX TAR creation.
		do
			last_status := c_set_tar (handle)
		end

	set_zip (handle: POINTER)
			-- Configure ZIP creation with DEFLATE compression.
		do
			last_status := c_set_zip (handle)
		end

	open_writer (handle: POINTER; descriptor: INTEGER)
			-- Attach the writer to a borrowed descriptor; Eiffel remains responsible for closing it.
		do
			last_status := c_write_open (handle, descriptor)
		end

	write_header (handle, entry: POINTER)
			-- Write an archive or extraction entry header.
		do
			last_status := c_write_header (handle, entry)
		end

	finish_entry (handle: POINTER)
			-- Finish the current archive or extraction entry.
		do
			last_status := c_write_finish (handle)
		end

	close_writer (handle: POINTER)
			-- Close an archive or extraction writer without freeing it.
		do
			last_status := c_write_close (handle)
		end

	free_writer (handle: POINTER)
			-- Consume a closed writer; diagnostics are unavailable afterwards.
		do
			last_status := c_write_free (handle)
		end

	open_reader (handle, path: POINTER)
			-- Enable available formats and filters and open a native archive path.
		do
			last_status := c_read_open (handle, path)
		end

	read_header (handle, entry_slot: POINTER)
			-- Advance the header and store a borrowed entry pointer; skip unread content.
		do
			last_status := c_read_next (handle, entry_slot)
		end

	close_reader (handle: POINTER)
			-- Close a reader without releasing its diagnostic storage.
		do
			last_status := c_read_close (handle)
		end

	free_reader (handle: POINTER)
			-- Consume a closed reader; diagnostics are unavailable afterwards.
		do
			last_status := c_read_free (handle)
		end

	configure_extraction (handle: POINTER)
			-- Protect extraction against parent paths, symlink traversal and overwrites.
		do
			last_status := c_disk_options (handle)
		end

	open_source (writer, metadata, entry, path, cleanup_code: POINTER)
			-- Open a source and publish its owned FILE pointer; preserve primary and cleanup diagnostics.
		do
			last_pointer := c_source_open (writer, metadata, entry, path, cleanup_code)
			if last_pointer = default_pointer then
				last_status := Archive_failed
			else
				last_status := Archive_ok
			end
		end

	pack_block (writer, source, byte_count: POINTER)
			-- Transfer one source block and write a uint64_t successful-byte count.
		do
			last_status := c_pack_block (writer, source, byte_count)
		end

	close_source (writer, source: POINTER)
			-- Consume an owned FILE and retain closing diagnostics in writer.
		do
			last_status := c_source_close (writer, source)
		end

	extract_block (reader, disk, byte_count: POINTER)
			-- Transfer one entry block, retaining every failure diagnostic in reader.
		do
			last_status := c_extract_block (reader, disk, byte_count)
		end

	create_output (path: POINTER)
			-- Exclusively create an output and publish its owned descriptor and captured errno.
		do
			last_descriptor := c_create_output (path, system_error_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
		end

	close_output (descriptor: INTEGER)
			-- Consume an output descriptor and publish a separate closing errno.
		do
			last_system_code := c_close_output (descriptor)
		end

	remove_output (path: POINTER)
			-- Unlink the selected output and publish a separate removal errno.
		do
			last_system_code := c_remove_output (path)
		end

	fail_writer (handle: POINTER)
			-- Mark a writer fatal so freeing it does not finalize an abandoned archive.
			-- The deliberate ARCHIVE_FATAL result is not a new operation failure.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"(void)archive_write_fail($handle);"
		end

	clear_entry_size (entry: POINTER)
			-- Mark a metadata-only entry as having no payload.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"archive_entry_set_size($entry, 0);"
		end

	set_seven_zip (handle: POINTER)
			-- Configure the libarchive 7z writer.
		do
			last_status := c_set_seven_zip (handle)
		end

	add_filter (handle, name: POINTER)
			-- Add the named outer filter supplied by libarchive.
		do
			last_status := c_add_filter (handle, name)
		end

	new_metadata_reader: POINTER
			-- Fresh owned disk-metadata reader; never used for directory traversal.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"struct archive *a = archive_read_disk_new(); if (!a) abort(); return a;"
		end

	configure_metadata (handle: POINTER)
			-- Collect physical-link metadata without ACL, xattr or filesystem flags.
		do
			last_status := c_configure_metadata (handle)
		end

	read_source_metadata (handle, entry, path: POINTER)
			-- Fill one owned entry from a native source path without traversing it.
		do
			last_status := c_read_source_metadata (handle, entry, path)
		end

	read_link_target (handle, entry: POINTER)
			-- Publish a borrowed UTF-8 symbolic-link target, or NULL on conversion failure.
		do
			last_pointer := c_read_link_target (handle, entry)
		end

	has_hardlink (entry: POINTER): BOOLEAN
			-- Does this entry declare a hardlink target?
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_entry_hardlink_is_set($entry);"
		end

	Symbolic_link_type: INTEGER
			-- Libarchive AE_IFLNK symbolic-link type.
		external
			"C macro use <libarchive_wrapper.h>"
		alias
			"AE_IFLNK"
		ensure
			instance_free: class
		end

	system_message (code: INTEGER): POINTER
			-- Borrowed diagnostic bytes for positive errno or negative Win32 code; copy immediately.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return (void *)libarchive_wrapper_system_message($code);"
		ensure
			instance_free: class
		end

	create_directory (path: POINTER)
			-- Exclusively create a directory and publish its owned physical path or captured errno.
		do
			last_pointer := c_create_directory (path, system_error_slot.item, system_cleanup_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
			last_cleanup_code := system_cleanup_slot.read_integer_32 (0)
		end

	free_path (path: POINTER)
			-- Release an owned physical path after its bytes have been copied.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"free($path);"
		end

	open_directory (path: POINTER)
			-- Publish an owned native directory iterator or a captured system error.
		do
			last_pointer := c_directory_open (path, system_error_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
		end

	read_directory_entry (directory: POINTER)
			-- Publish one borrowed native name; distinguish EOF from an errno failure.
		do
			last_pointer := c_directory_next (directory, system_error_slot.item)
			last_system_code := system_error_slot.read_integer_32 (0)
		end

	close_directory (directory: POINTER)
			-- Consume one native directory iterator and publish a captured closing error.
		do
			last_system_code := c_directory_close (directory)
		end

feature {NONE} -- C ABI result functions

	c_is_output_source (descriptor: INTEGER; path, code_slot, cleanup_slot: POINTER): BOOLEAN
			-- Checked native identity comparison against an owned descriptor.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_is_output_source"
		end

	c_source_type (path, code_slot, cleanup_slot: POINTER): INTEGER
			-- Physical native type with captured system error; never follow a reparse target.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_source_type"
		end

	c_create_directory (path, code_slot, cleanup_slot: POINTER): POINTER
			-- Owned physical native path after exclusive root creation, or NULL with separate errors.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_create_directory"
		end

	c_entry_name (reader, entry: POINTER): POINTER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($reader, return NULL); errno = 0; const char *p = archive_entry_pathname_utf8($entry); if (!p && errno == ENOMEM) abort(); LIBARCHIVE_WRAPPER_LEAVE(); return (void *)p;"
		end

	c_set_tar (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_write_set_format_pax_restricted($handle);"
		end

	c_set_zip (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int r = archive_write_set_format_zip($handle); if (r == ARCHIVE_OK) r = archive_write_set_format_option($handle, %"zip%", %"compression%", %"deflate%"); return r;"
		end

	c_write_open (handle: POINTER; descriptor: INTEGER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_write_open_fd($handle, $descriptor); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_write_header (handle, entry: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_write_header($handle, $entry); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_write_finish (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_write_finish_entry($handle); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_write_close (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_write_close($handle); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_write_free (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_write_free($handle);"
		end

	c_read_open (handle, path: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return libarchive_wrapper_open_reader($handle, $path);"
		end

	c_read_next (handle, entry_slot: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_read_next_header($handle, (struct archive_entry **)$entry_slot); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_read_close (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); int r = archive_read_close($handle); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_read_free (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_read_free($handle);"
		end

	c_disk_options (handle: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_write_disk_set_options($handle, ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_NO_OVERWRITE);"
		end

	c_source_open (writer, metadata, entry, path, cleanup_code: POINTER): POINTER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_source_open"
		end

	c_pack_block (writer, source, byte_count: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_pack_block"
		end

	c_source_close (writer, source: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_source_close"
		end

	c_extract_block (reader, disk, byte_count: POINTER): INTEGER
			-- C ABI result of a state-changing operation; used only by command adapters.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_extract_block"
		end

	c_create_output (path, code_slot: POINTER): INTEGER
			-- C ABI result of exclusive binary output creation, with captured system error.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_create_output"
		end

	c_close_output (descriptor: INTEGER): INTEGER
			-- Captured errno after consuming a descriptor, or zero.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_close_output"
		end

	c_remove_output (path: POINTER): INTEGER
			-- Captured errno after unlink, or zero.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_remove_output"
		end

	c_set_seven_zip (handle: POINTER): INTEGER
			-- C ABI result of configuring 7z creation.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_write_set_format_7zip($handle);"
		end

	c_add_filter (handle, name: POINTER): INTEGER
			-- C ABI result of configuring an outer compression filter.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_write_add_filter_by_name($handle, $name);"
		end

	c_configure_metadata (handle: POINTER): INTEGER
			-- C ABI result of configuring physical, minimal source metadata.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int r = archive_read_disk_set_symlink_physical($handle); if (r == ARCHIVE_OK) r = archive_read_disk_set_behavior($handle, ARCHIVE_READDISK_NO_ACL | ARCHIVE_READDISK_NO_XATTR | ARCHIVE_READDISK_NO_FFLAGS); return r;"
		end

	c_read_source_metadata (handle, entry, path: POINTER): INTEGER
			-- C ABI result of collecting one source entry, including its symlink target.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return ARCHIVE_FATAL); libarchive_wrapper_set_native_name($entry, $path); int r = archive_read_disk_entry_from_file($handle, $entry, -1, NULL); LIBARCHIVE_WRAPPER_LEAVE(); return r;"
		end

	c_read_link_target (handle, entry: POINTER): POINTER
			-- Borrowed UTF-8 symlink target obtained inside a native locale scope.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"LIBARCHIVE_WRAPPER_ENTER($handle, return NULL); errno = 0; const char *p = archive_entry_symlink_utf8($entry); if (!p && errno == ENOMEM) abort(); LIBARCHIVE_WRAPPER_LEAVE(); return (void *)p;"
		end

	c_directory_open (path, code_slot: POINTER): POINTER
			-- C ABI result of opening one checked directory iterator.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_directory_open"
		end

	c_directory_next (directory, code_slot: POINTER): POINTER
			-- Borrowed C ABI name pointer, with zero errno at normal EOF.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_directory_next"
		end

	c_directory_close (directory: POINTER): INTEGER
			-- Captured closing errno after consuming a directory iterator, or zero.
		external
			"C use <libarchive_wrapper.h>"
		alias
			"libarchive_wrapper_directory_close"
		end

	system_cleanup_slot: MANAGED_POINTER
			-- Separate system cleanup diagnostic for a multi-call native primitive.

	system_error_slot: MANAGED_POINTER
			-- Standalone int32_t errno output, independent of libarchive diagnostics.

feature {ARC_READER, ARC_WRITER} -- Inspection and passwords

	add_read_password (handle, password: POINTER)
			-- Register a UTF-8 passphrase before opening input.
		do
			last_status := c_add_read_password (handle, password)
		end

	set_write_password (handle, password: POINTER)
			-- Set the UTF-8 output passphrase before opening output.
		do
			last_status := c_set_write_password (handle, password)
		end

	enable_zip_encryption (handle: POINTER)
			-- Enable ZIP AES-256 before opening output.
		do
			last_status := c_enable_zip_encryption (handle)
		end

	consume_block (handle: POINTER)
			-- Read and discard one block without advancing the current header.
		do
			last_status := c_consume_block (handle)
		end

	format_name (handle: POINTER): POINTER
			-- Borrowed detected format name, or NULL.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return (void *)archive_format_name($handle);"
		end

	filter_count (handle: POINTER): INTEGER
			-- Number of detected filters, including the terminal none filter.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_filter_count($handle);"
		end

	filter_name (handle: POINTER; index: INTEGER): POINTER
			-- Borrowed filter name at a native zero-based index.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return (void *)archive_filter_name($handle, $index);"
		end

	encryption_status (handle: POINTER): INTEGER
			-- Native encryption detection status.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return archive_read_has_encrypted_entries($handle);"
		end

feature {NONE} -- Password and consumption primitives

	c_add_read_password (handle, password: POINTER): INTEGER
			-- Native status of the corresponding command.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int r = archive_read_add_passphrase($handle, $password); if (r != ARCHIVE_OK && archive_errno($handle) == ENOMEM) abort(); return r;"
		end

	c_set_write_password (handle, password: POINTER): INTEGER
			-- Native status of the corresponding command.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int r = archive_write_set_passphrase($handle, $password); if (r != ARCHIVE_OK && archive_errno($handle) == ENOMEM) abort(); return r;"
		end

	c_enable_zip_encryption (handle: POINTER): INTEGER
			-- Native status of the corresponding command.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"int r = archive_write_set_format_option($handle, %"zip%", %"encryption%", %"aes256%"); if (r != ARCHIVE_OK && archive_errno($handle) == ENOMEM) abort(); return r;"
		end

	c_consume_block (handle: POINTER): INTEGER
			-- Native status of the corresponding command.
		external
			"C inline use <libarchive_wrapper.h>"
		alias
			"return libarchive_wrapper_consume_block($handle);"
		end

end
