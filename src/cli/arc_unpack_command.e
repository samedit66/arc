note
	description: "Archive extraction command using the library API."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_UNPACK_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "unpack"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Extract an archive"
			-- Command summary displayed in application help.

feature -- Interface definition

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (a_input: CLI_COMMAND_INPUT)
			-- Extract beside the source; report operation failures with exit status 1.
		local
			archive: ARCHIVE_READER
			options: UNPACK_OPTIONS
			archive_name, password, diagnostic: STRING_32
			display: ARC_ENTRY_DISPLAY
			failure: ARCHIVE_ERROR
		do
			archive_name := a_input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end

			create archive.make_with_name (archive_name)
			create display
			create options.make
			password := a_input.string_parameter ("password")
			if not password.is_empty then
				options.set_password (password)
			end

			archive.unpack_with_options (options)

			if not archive.is_successful then
				failure := archive.last_error
				diagnostic := display.failure_message ("unpack", archive.path, failure)
				fail (diagnostic, 1)
			end
		end

end
