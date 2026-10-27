note
	description: "Application help and version command."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ROOT_COMMAND

inherit
	CLI_COMMAND

create
	make

feature {NONE} -- Initialization

	make (an_application: CLI_APPLICATION)
			-- Associate the root command with `an_application'.
		do
			application := an_application
		end

feature -- Definition

	name: STRING_32 = ""
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Show application help or version"
			-- Command summary displayed in application help.

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_flag ("help", "Show application help")
			set_short_name ("help", 'h')
			add_flag ("version", "Show application and libarchive versions")
		end

feature -- Execution

	handle (a_input: CLI_COMMAND_INPUT)
			-- Show root help or the application and linked libarchive versions.
		do
			if a_input.boolean_parameter ("help") or else not a_input.boolean_parameter ("version") then
				io.put_string_32 (application.help)
				io.put_string ("%NRoot options:%N  -h, --help  Show application help%N  --version   Show application and libarchive versions%N")
			else
				io.put_string ("arc dev%Nlibarchive version number: ")
				io.put_integer (libarchive_version)
				io.put_new_line
			end
		end

feature {NONE} -- Implementation

	application: CLI_APPLICATION
			-- CLI application whose help is displayed by this command.

	libarchive_version: INTEGER
			-- Linked libarchive version number.
		external
			"C use <archive.h>"
		alias
			"archive_version_number"
		end

end
