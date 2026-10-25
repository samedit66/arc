note
	description: "Application entry point for the Arc command-line interface."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_CLI

create
	make

feature {NONE} -- Initialization

	make
			-- Run the archive command-line interface.
		local
			application: CLI_APPLICATION
		do
			create application.make ("arc", "Create, extract, list, inspect and verify archives.")
			application.add_command (create {ARC_ROOT_COMMAND}.make (application))
			application.add_command (create {ARC_PACK_COMMAND})
			application.add_command (create {ARC_UNPACK_COMMAND})
			application.add_command (create {ARC_LIST_COMMAND})
			application.add_command (create {ARC_INFO_COMMAND})
			application.add_command (create {ARC_VERIFY_COMMAND})
			application.run
		end

end
