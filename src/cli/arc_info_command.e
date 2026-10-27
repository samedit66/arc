note
	description: "Archive summary from sequential reader metadata."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_INFO_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "info"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Show archive information"
			-- Command summary displayed in application help.

feature -- Interface definition

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (input: CLI_COMMAND_INPUT)
			-- Inspect headers and print the summary only after successful closing.
		local
			reader: ARC_ARCHIVE
			path: PATH
		do
			if input.string_parameter ("archive").is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end
			create path.make_from_string (input.string_parameter ("archive"))
			if input.string_parameter ("password").is_empty then
				create reader.make_open_read (path)
			else
				create reader.make_open_read_with_password (path, input.string_parameter ("password"))
			end

			if not reader.has_failed then
				reader.inspect_remaining
			end
			reader.close
			if reader.has_failed then
				fail ({ARC_ENTRY_DISPLAY}.failure_message ("inspect", path, reader.last_error), 1)
			end

			check attached reader.inspection as summary then
				{ARC_ENTRY_DISPLAY}.put_info (path, summary)
			end
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
		end

end
