note
	description: "Declaration of the archive verification command."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_VERIFY_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "verify"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Verify an archive"
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
			-- Read all archive contents without extraction; report failures with exit status 1.
		local
			archive: ARCHIVE_READER
			options: ARCHIVE_READ_OPTIONS
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

			archive.verify_with_options (options)

			if not archive.is_successful then
				failure := archive.last_error
				diagnostic := display.failure_message ("verify", archive.path, failure)
				fail (diagnostic, 1)
			end
		end

end
