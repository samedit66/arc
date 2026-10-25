note
	description: "Declaration of the archive listing command."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_LIST_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "list"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "List archive entries"
			-- Command summary displayed in application help.

feature -- Interface definition

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_flag ("tree", "Show entries as a tree")
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (a_input: CLI_COMMAND_INPUT)
			-- List stored pathnames without extraction; publish a tree only after complete success.
		local
			stream: detachable ARCHIVE_READ_STREAM
			options: ARCHIVE_READ_OPTIONS
			archive_name, password, diagnostic: STRING_32
			failure: ARCHIVE_ERROR
			tree: detachable ARC_LIST_TREE
			display: ARC_ENTRY_DISPLAY
		do
			archive_name := a_input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end

			create display
			create options.make
			password := a_input.string_parameter ("password")
			if not password.is_empty then
				options.set_password (password)
			end
			if a_input.boolean_parameter ("tree") then
				create tree.make (".")
			end
			create stream.make_with_options (create {PATH}.make_from_string (archive_name), options)

			stream.open
			across stream as entry loop
				if attached tree as nodes then
					nodes.add (entry)
				else
					display.put_line (display.escaped (entry.pathname))
				end
			end
			stream.close

			if stream.has_failed then
				failure := stream.last_error
				diagnostic := display.failure_message ("list", stream.path, failure)
				fail (diagnostic, 1)
			elseif attached tree as nodes then
				nodes.print_tree ("")
			end
		rescue
			if attached stream as active_stream then
				active_stream.close
			end
		end

end
