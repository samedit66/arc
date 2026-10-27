note
	description: "Validated POSIX archive extraction through the ARC block reader."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_UNPACK_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "unpack"
			-- Inherited CLI command name.

	description: STRING_32 = "Extract an archive"
			-- Application help summary.

feature -- Interface definition

	interface
			-- Declare archive selection and optional byte progress.
		do
			add_flag ("progress", "Show per-file progress on stderr")
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (input: CLI_COMMAND_INPUT)
			-- Restore entries in a new sibling directory after validating original POSIX names.
		local
			reader: ARC_ARCHIVE
			archive_path, destination: PATH
			archive_name: STRING_32
			display: ARC_ENTRY_DISPLAY
		do
			archive_name := input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end
			archive_path := (create {PATH}.make_from_string (archive_name)).absolute_path
			if attached archive_path.entry as entry then
				destination := archive_path.parent.extended (unpack_name (entry.name)).canonical_path
			else
				destination := archive_path.parent.extended ("archive.unpacked")
			end
			if input.string_parameter ("password").is_empty then
				create reader.make_open_read (archive_path)
			else
				create reader.make_open_read_with_password (archive_path, input.string_parameter ("password"))
			end
			if reader.has_failed then
				reader.close
				fail ({ARC_ENTRY_DISPLAY}.failure_message ("unpack", archive_path, reader.last_error), 1)
			end
			if input.boolean_parameter ("progress") then
				create display.make_progress
				reader.set_progress (agent display.update_progress)
			end
			reader.extract_all (destination)
			if attached reader.extraction_directory as physical_path then
				destination := physical_path
			end
			reader.close
			if reader.has_failed then
				fail ({ARC_ENTRY_DISPLAY}.failure_message ("unpack", archive_path, reader.last_error), 1)
			end
			{ARC_ENTRY_DISPLAY}.put_line ({STRING_32} "Extracted to " + {ARC_ENTRY_DISPLAY}.escaped (destination.name))
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
		end

feature {NONE} -- Destination policy

	unpack_name (filename: READABLE_STRING_32): STRING_32
			-- Destination basename with a recognized read suffix removed, or `.unpacked' appended.
		local
			suffix, lower: STRING_32
		do
			Result := filename.to_string_32.twin
			lower := Result.as_lower
			suffix := {ARC_FORMATS}.output_suffix (filename)
			across <<".rar", ".cpio", ".xar", ".iso">> as candidate loop
				if lower.ends_with (candidate) and candidate.count > suffix.count then
					suffix := candidate.to_string_32
				end
			end
			if not suffix.is_empty and Result.count > suffix.count then
				Result.keep_head (Result.count - suffix.count)
			else
				Result.append_string_general (".unpacked")
			end
		ensure
			instance_free: class
		end

end
