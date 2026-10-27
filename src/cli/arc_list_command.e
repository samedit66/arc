note
	description: "Sequential flat and tree listings through immutable ARC reader metadata."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_LIST_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "list"
			-- Inherited CLI command name.

	description: STRING_32 = "List archive entries"
			-- Application help summary.

feature -- Interface definition

	interface
			-- Declare flat/tree selection and the archive pathname.
		do
			add_flag ("tree", "Show entries as a tree")
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (input: CLI_COMMAND_INPUT)
			-- List entries without extracting bodies; publish a tree only after successful closing.
		local
			reader: ARC_ARCHIVE
			tree: detachable ARC_LIST_TREE
			path: PATH
			archive_name: STRING_32
		do
			archive_name := input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end
			create path.make_from_string (archive_name)
			if input.boolean_parameter ("tree") then
				create tree.make (".")
			end
			if input.string_parameter ("password").is_empty then
				create reader.make_open_read (path)
			else
				create reader.make_open_read_with_password (path, input.string_parameter ("password"))
			end
			from until reader.after loop
				if attached tree as nodes then
					nodes.add (reader.entry_name, {ARC_ENTRY_DISPLAY}.kind_name (reader), reader.entry_is_directory)
				else
					{ARC_ENTRY_DISPLAY}.put_line ({ARC_ENTRY_DISPLAY}.escaped (reader.entry_name))
				end
				reader.forth
			end
			reader.close
			if reader.has_failed then
				fail ({ARC_ENTRY_DISPLAY}.failure_message ("list", path, reader.last_error), 1)
			elseif attached tree as nodes then
				nodes.print_tree ("")
			end
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
		end

end
