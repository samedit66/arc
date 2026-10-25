note
	description: "Archive creation command using the library API."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_PACK_COMMAND

inherit
	CLI_COMMAND

	ARCHIVE_FORMATS

feature -- Definition

	name: STRING_32 = "pack"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Create an archive"
			-- Command summary displayed in application help.

feature -- Interface definition

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_optional_string_option ("format", "Container: zip, tar or 7z; otherwise infer from suffix", "")
			add_flag ("keep-root", "Retain the name of a single source directory")
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
			add_optional_string_list_argument ("source", "Sources; default to the archive basename directory in the current directory", {ARRAY [STRING_32]} << >>)
		end

feature -- Execution

	handle (a_input: CLI_COMMAND_INPUT)
			-- Create an archive with original Arc source mapping and encoding rules.
		local
			writer: ARCHIVE_WRITER
			options: PACK_OPTIONS
			sources: ARRAYED_LIST [STRING_32]
			archive_name, password, source_name, diagnostic: STRING_32
			output_path: PATH
			info: FILE_INFO
			failure: ARCHIVE_ERROR
			keep_root, implicit_source: BOOLEAN
			suffix: STRING_32
			display: ARC_ENTRY_DISPLAY
		do
			archive_name := a_input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end

			create display
			create options.make
			suffix := output_suffix (archive_name)
			configure_encoding (options, suffix, a_input.string_parameter ("format"))
			password := a_input.string_parameter ("password")
			if not password.is_empty then
				options.set_password (password)
			end

			sources := a_input.string_list_parameter ("source")
			implicit_source := sources.is_empty
			if implicit_source then
				create output_path.make_from_string (archive_name)
				if attached output_path.entry as entry then
					source_name := entry.name.to_string_32
					source_name.keep_head (source_name.count - suffix.count)
				else
					create source_name.make_empty
				end
				if source_name.is_empty or else source_name.same_string (".") or else source_name.same_string ("..") then
					fail ("Cannot derive a source directory; specify sources explicitly", Invalid_call)
				end
				sources.extend (source_name)
			end

			keep_root := a_input.boolean_parameter ("keep-root")
			if keep_root and sources.count /= 1 then
				fail ("Keeping the root requires one directory", Invalid_call)
			end

			create writer.make_with_name (archive_name)
			create info.make
			info.set_is_following_symlinks (False)
			across sources as source loop
				register_source (writer, source, info, sources.count = 1, keep_root, implicit_source)
			end

			writer.pack_with_options (options)

			if not writer.is_successful then
				failure := writer.last_error
				diagnostic := display.failure_message ("create", writer.path, failure)
				fail (diagnostic, 1)
			end

			display.put_line ({STRING_32} "Created " + display.escaped (archive_name))
		end

feature {NONE} -- Source registration

	register_source (writer: ARCHIVE_WRITER; source: STRING_32; info: FILE_INFO; single_source, keep_root, implicit_source: BOOLEAN)
			-- Validate CLI input before calling the contracted registration API.
		local
			source_path: PATH
			is_directory: BOOLEAN
			display: ARC_ENTRY_DISPLAY
		do
			create display
			if source.is_empty then
				fail ("Source path must not be empty", Invalid_call)
			end

			create source_path.make_from_string (source)
			info.update (source_path.name)
			if not info.exists then
				fail ({STRING_32} "Source not found: " + display.escaped (source), 1)
			end
			is_directory := info.is_directory and not info.is_symlink
			if implicit_source and not is_directory then
				fail ("Inferred source is not a directory; specify sources explicitly", 1)
			elseif keep_root and not is_directory then
				fail ("Keeping the root requires one directory", Invalid_call)
			end

			if single_source and is_directory and not keep_root then
				writer.add_directory_contents (source_path)
			else
				if not writer.has_source_name (source_path) then
					fail ({STRING_32} "Source has no valid archive basename: " + display.escaped (source), 1)
				end
				writer.add_path (source_path)
			end
		end

feature {NONE} -- Encoding

	configure_encoding (options: PACK_OPTIONS; suffix, explicit_format: STRING_32)
			-- Select the container and suffix compression, rejecting conflicting formats.
		local
			format, compression, inferred_format: INTEGER
		do
			compression := compression_for_suffix (suffix)
			inferred_format := format_for_suffix (suffix)

			if explicit_format.is_empty then
				format := inferred_format
			elseif explicit_format.same_string ("zip") then
				format := options.Zip
			elseif explicit_format.same_string ("tar") then
				format := options.Tar
			elseif explicit_format.same_string ("7z") then
				format := options.Seven_zip
			else
				fail ("Unsupported format; use zip, tar or 7z", 1)
			end

			if format = Unknown_format then
				fail ("Cannot infer output format; specify --format", Invalid_call)
			elseif inferred_format /= Unknown_format and inferred_format /= format then
				fail ("--format conflicts with the output suffix", Invalid_call)
			end

			options.set_encoding (format, compression)
		end

end
