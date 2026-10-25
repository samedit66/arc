note
    description: "Explicit help commands for an application and its registered commands."
    author: "samedit66"
    email: "samedit66@yandex.ru"

class
    CLI_HELP_COMMAND

inherit
    CLI_COMMAND

create
    make

feature {NONE} -- Initialization

    make (a_application: CLI_APPLICATION)
            -- Create a help command for `a_application`.
        do
            application := a_application
        end

feature -- Definition

    name: STRING_32 = "help"
            -- Command name used for selection.

    description: STRING_32 = "Show application or command help"
            -- Description shown in generated help.

feature -- Interface definition

    interface
            -- Declare the command parameters.
        do
            add_optional_string_list_argument ("command", "Command words to describe", {ARRAY [STRING_32]} <<>>)
        end

feature -- Execution

    handle (a_values: CLI_COMMAND_INPUT)
            -- Show application help or help for the supplied command words.
        local
            l_name: STRING_32
        do
            create l_name.make_empty
            across a_values.string_list_parameter ("command") as word loop
                if not l_name.is_empty then
                    l_name.append_character (' ')
                end
                l_name.append (word)
            end
            if l_name.is_empty then
                io.put_string_32 (application.help)
            else
                application.show_help (l_name)
            end
        end

feature {NONE} -- Implementation

    application: CLI_APPLICATION

end
