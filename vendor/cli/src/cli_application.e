note
    description: "Command-line applications with command selection and generated help."
    author: "samedit66"
    email: "samedit66@yandex.ru"

class
    CLI_APPLICATION

inherit
    CLI_FAILURE
        export
            {NONE} fail
        end

create
    make,
    make_single_command

feature {NONE} -- Initialization

    make (a_name, a_description: READABLE_STRING_32)
            -- Create an application with `a_name` and `a_description`.
        require
            non_empty_name: not a_name.is_empty
        do
            create name.make_from_string (a_name)
            create description.make_from_string (a_description)
            create commands.make (Default_capacity)
        end

    make_single_command (a_name: READABLE_STRING_32; a_command: CLI_COMMAND)
            -- Create an application using one default command and its description.
        require
            non_empty_name: not a_name.is_empty
            default_command: a_command.name.is_empty
        do
            make (a_name, a_command.description)
            add_command (a_command)
        end

feature -- Definition

    name: IMMUTABLE_STRING_32
            -- Application name used in help and invocation examples.

    description: IMMUTABLE_STRING_32
            -- Description shown in generated help.

    add_command (a_command: CLI_COMMAND)
            -- Register a command and declare its interface.
        require
            valid_name: is_valid_command_name (a_command.name)
            unique_name: not has_command (a_command.name)
        do
            a_command.initialize_interface
            commands.extend (a_command)
        ensure
            registered: has_command (a_command.name)
        end

    is_valid_command_name (a_name: READABLE_STRING_32): BOOLEAN
            -- Is this an empty default name or a canonical sequence of non-option words?
        do
            Result := not a_name.starts_with (" ") and then
                not a_name.ends_with (" ") and then not a_name.has_substring ("  ") and then
                not a_name.has ('%T') and then not a_name.has ('%N') and then
                not a_name.has ('%R') and then
                across a_name.split (' ') as word all not word.starts_with ("-") end
        end

    has_command (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a command with `a_name` registered?
        do
            Result := across commands as c some c.name.same_string (a_name) end
        end

feature -- Help

    help: STRING_32
            -- Fresh application help, in command registration order.
        do
            Result := name.to_string_32 + " - " + description + "%N%NUsage:%N  " + name
            if has_command ("") then
                Result.append (" [command]")
            else
                Result.append (" <command>")
            end
            Result.append (" [arguments and options]%N%NCommands:%N")
            across commands as c loop
                if c.name.is_empty then
                    Result.append ({STRING_32} "  (default)  " + c.description + "%N")
                else
                    Result.append ({STRING_32} "  " + c.name + "  " + c.description + "%N")
                end
            end
        end

    command_help (a_name: READABLE_STRING_32): STRING_32
            -- Fresh help for the registered command named `a_name`.
        require
            known_command: has_command (a_name)
        do
            create Result.make_empty
            across commands as c loop
                if c.name.same_string (a_name) then
                    if c.name.is_empty then
                        Result := c.help (name)
                    else
                        Result := c.help (name.to_string_32 + " " + c.name)
                    end
                end
            end
        end

    show_help (a_command_name: READABLE_STRING_32)
            -- Print command help, or the overview when no default command exists.
        do
            if a_command_name.is_empty and then not has_command ("") then
                io.put_string_32 (help)
            elseif has_command (a_command_name) then
                io.put_string_32 (command_help (a_command_name))
            else
                report_unknown_command (a_command_name)
            end
        end

feature -- Execution

    run
            -- Execute using process arguments, excluding the executable name.
            -- Invalid calls terminate the process with {CLI_COMMAND}.Invalid_call.
        local
            l_arguments: ARGUMENTS_32
            l_tokens: ARRAYED_LIST [READABLE_STRING_32]
            i: INTEGER
        do
            create l_arguments
            create l_tokens.make (l_arguments.argument_count)
            from i := 1 until i > l_arguments.argument_count loop
                l_tokens.extend (l_arguments.argument (i))
                i := i + 1
            end

            run_with_tokens (l_tokens)
        end

    run_with_tokens (a_tokens: READABLE_INDEXABLE [READABLE_STRING_32])
            -- Select and execute a command. Tokens exclude the executable name.
            -- Invalid calls terminate the process with {CLI_COMMAND}.Invalid_call.
        local
            l_match: detachable TUPLE [command: CLI_COMMAND; consumed_count: INTEGER]
            l_tokens: ARRAYED_LIST [READABLE_STRING_32]
            l_parser: CLI_PARSER
            i: INTEGER
        do
            l_match := selected_command (a_tokens)
            if attached l_match as match then
                create l_tokens.make (a_tokens.upper - a_tokens.lower + 1 - match.consumed_count)
                from i := a_tokens.lower + match.consumed_count until i > a_tokens.upper loop
                    l_tokens.extend (a_tokens [i])
                    i := i + 1
                end
                create l_parser.make (match.command.parameters)
                l_parser.parse (l_tokens)
                if l_parser.help_requested then
                    io.put_string_32 (command_help (match.command.name))
                elseif l_parser.is_successful then
                    match.command.handle (l_parser.values)
                else
                    report_error (l_parser.error_message)
                end
            elseif a_tokens.upper = a_tokens.lower and then (a_tokens [a_tokens.lower].same_string ("--help") or else a_tokens [a_tokens.lower].same_string ("-h")) then
                show_help ("")
            elseif a_tokens.lower > a_tokens.upper then
                report_error ("Missing command.")
            else
                report_unknown_command (a_tokens [a_tokens.lower])
            end
        end

feature {NONE} -- Implementation

    selected_command (a_tokens: READABLE_INDEXABLE [READABLE_STRING_32]): detachable TUPLE [command: CLI_COMMAND; consumed_count: INTEGER]
            -- Command with the longest complete word prefix, including an empty default.
        local
            l_words: LIST [READABLE_STRING_32]
            l_count, i: INTEGER
            l_matches: BOOLEAN
        do
            across commands as c loop
                l_count := 0
                l_matches := True
                if not c.name.is_empty then
                    l_words := c.name.split (' ')
                    l_count := l_words.count
                    l_matches := l_count <= a_tokens.upper - a_tokens.lower + 1
                    from i := 1 until i > l_count or not l_matches loop
                        l_matches := l_words [i].same_string (a_tokens [a_tokens.lower + i - 1])
                        i := i + 1
                    end
                end
                if l_matches and then (not attached Result as best or else l_count > best.consumed_count) then
                    Result := [c, l_count]
                end
            end
        end

    commands: ARRAYED_LIST [CLI_COMMAND]

    report_unknown_command (a_name: READABLE_STRING_32)
        do
            report_error ({STRING_32} "Unknown command: " + a_name)
        end

    report_error (a_message: READABLE_STRING_32)
        do
            fail (a_message, {CLI_COMMAND}.Invalid_call)
        ensure
            never_returns: False
        end

feature {NONE} -- Capacity

    Default_capacity: INTEGER = 4
            -- Initial capacity of internal collections.

end
