note
    description: "Command definitions and handlers for typed command-line input."
    author: "samedit66"
    email: "samedit66@yandex.ru"

deferred class
    CLI_COMMAND

inherit
    CLI_STRING_ROUTINES
        export
            {NONE} split
        end

    CLI_FAILURE
        export
            {ANY} fail
        end

feature -- Definition

    name: READABLE_STRING_32
            -- Command name; its value remains stable after registration.
        deferred
        end

    description: READABLE_STRING_32
            -- Command description.
        deferred
        end

feature {CLI_APPLICATION} -- Interface definition

    interface
            -- Declare arguments, options, and flags.
        deferred
        end

feature {CLI_APPLICATION} -- Initialization

    initialize_interface
            -- Declare parameters once and supply automatic help when needed.
        local
            l_help: CLI_PARAMETER
        do
            if not is_interface_initialized then
                interface
                if not has_parameter ("help") then
                    create l_help.make_help
                    automatic_help := l_help
                    register_parameter (l_help)
                    if can_set_short_name ("help", 'h') then
                        set_short_name ("help", 'h')
                    end
                end
                is_interface_initialized := True
            end
        end

feature -- Status report

    is_interface_initialized: BOOLEAN
            -- Has the application declared this command's interface?

feature -- Integer parameters

    add_integer_argument (
        a_name, a_description: READABLE_STRING_32
    )
            -- Add a required positional integer argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
            required_argument_before_optional: not has_optional_argument
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_integer_argument (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_integer_argument (
        a_name, a_description: READABLE_STRING_32;
        a_default: INTEGER
    )
            -- Add an optional positional integer argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_integer_argument (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

    add_integer_option (
        a_name, a_description: READABLE_STRING_32
    )
            -- Add a required integer option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_integer_option (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_integer_option (
        a_name, a_description: READABLE_STRING_32;
        a_default: INTEGER
    )
            -- Add an optional integer option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_integer_option (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

feature -- String parameters

    add_string_argument (
        a_name, a_description: READABLE_STRING_32
    )
            -- Add a required positional string argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
            required_argument_before_optional: not has_optional_argument
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_string_argument (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_string_argument (
        a_name, a_description: READABLE_STRING_32;
        a_default: READABLE_STRING_32
    )
            -- Add an optional positional string argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_string_argument (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

    add_string_option (
        a_name, a_description: READABLE_STRING_32
    )
            -- Add a required string option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_string_option (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_string_option (
        a_name, a_description: READABLE_STRING_32;
        a_default: READABLE_STRING_32
    )
            -- Add an optional string option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_string_option (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

feature -- List parameters

    add_integer_list_argument (a_name, a_description: READABLE_STRING_32)
            -- Add a required integer list argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
            required_argument_before_optional: not has_optional_argument
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_integer_list_argument (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_integer_list_argument (a_name, a_description: READABLE_STRING_32; a_default: ITERABLE [INTEGER])
            -- Add a defaulted integer list argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_integer_list_argument (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

    add_integer_list_option (a_name, a_description: READABLE_STRING_32)
            -- Add a required integer list option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_integer_list_option (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_integer_list_option (a_name, a_description: READABLE_STRING_32; a_default: ITERABLE [INTEGER])
            -- Add a defaulted integer list option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_integer_list_option (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

    add_string_list_argument (a_name, a_description: READABLE_STRING_32)
            -- Add a required string list argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
            required_argument_before_optional: not has_optional_argument
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_string_list_argument (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_string_list_argument (a_name, a_description: READABLE_STRING_32; a_default: ITERABLE [READABLE_STRING_32])
            -- Add a defaulted string list argument.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
            positional_list_last: not has_positional_list
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_string_list_argument (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

    add_string_list_option (a_name, a_description: READABLE_STRING_32)
            -- Add a required string list option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_string_list_option (a_name, a_description)
            register_parameter (l_parameter)
        end

    add_optional_string_list_option (a_name, a_description: READABLE_STRING_32; a_default: ITERABLE [READABLE_STRING_32])
            -- Add a defaulted string list option.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_optional_string_list_option (a_name, a_description, a_default)
            register_parameter (l_parameter)
        end

feature -- Flags

    add_flag (
        a_name, a_description: READABLE_STRING_32
    )
            -- Add a flag: False when absent, True when present.
        require
            valid_name: is_valid_parameter_name (a_name)
            unique_name: is_available_parameter_name (a_name)
        local
            l_parameter: CLI_PARAMETER
        do
            create l_parameter.make_flag (a_name, a_description)
            register_parameter (l_parameter)
        end

feature -- Short names

    set_short_name (
        a_name: READABLE_STRING_32;
        a_short_name: CHARACTER_32
    )
            -- Assign a short name to an option or flag.
            -- Short names are letters and can be assigned only once.
            -- A user assignment may claim the alias of automatic help.
        require
            assignable: can_set_short_name (a_name, a_short_name)
        do
            if attached automatic_help as l_help and then not a_name.same_string (l_help.name) and then
                l_help.has_short_name and then l_help.short_name = a_short_name
            then
                l_help.remove_short_name
            end
            across parameters as p loop
                if p.name.same_string (a_name) then
                    p.set_short_name (a_short_name)
                end
            end
        ensure
            assigned: across parameters as p all
                p.name.same_string (a_name) implies
                    (p.has_short_name and then p.short_name = a_short_name) end
        end

feature -- Parameter availability

    is_available_parameter_name (a_name: READABLE_STRING_32): BOOLEAN
            -- Is this name free or occupied only by automatic help?
        do
            Result := not has_parameter (a_name) or else
                (a_name.same_string ("help") and then automatic_help /= Void)
        end

    has_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a parameter with this name already declared?
        do
            Result := across parameters as p some p.name.same_string (a_name) end
        end

    has_positional_list: BOOLEAN
            -- Is a positional list already declared?
        do
            Result := across parameters as p some p.kind = p.Argument_kind and then p.is_multiple end
        end

    has_optional_argument: BOOLEAN
            -- Is an optional positional argument already declared?
        do
            Result := across parameters as p some p.kind = p.Argument_kind and then p.is_optional end
        end

feature -- Short name availability

    can_set_short_name (a_name: READABLE_STRING_32; a_short_name: CHARACTER_32): BOOLEAN
            -- Can an unassigned option or flag receive this unique letter?
        do
            Result := a_short_name.is_alpha and then
                (across parameters as p some
                    p.name.same_string (a_name) and then
                    p.kind /= p.Argument_kind and then not p.has_short_name end) and then
                (across parameters as p all
                    p = automatic_help or else not p.has_short_name or else p.short_name /= a_short_name end)
        end

feature -- Help

    help (a_invocation: READABLE_STRING_32): STRING_32
            -- Fresh help using the complete invocation, including any subcommand name.
        local
            l_has_options: BOOLEAN
        do
            Result := a_invocation.to_string_32 + " - " + description +
                "%N%NUsage:%N  " + a_invocation
            across parameters as p loop
                if p.kind = p.Argument_kind then
                    if p.is_optional then
                        Result.append ({STRING_32} " [" + argument_usage_name (p) + "]")
                    else
                        Result.append ({STRING_32} " <" + argument_usage_name (p) + ">")
                    end
                elseif p.kind = p.Option_kind and then not p.is_optional then
                    Result.append ({STRING_32} " --" + p.name + " <" + parameter_type_name (p) + ">")
                else
                    l_has_options := True
                end
            end
            if l_has_options then
                Result.append (" [options]")
            end
            Result.append_character ('%N')
            append_help_section (Result, "Arguments", 1)
            append_help_section (Result, "Required options", 2)
            append_help_section (Result, "Optional options", 3)
            append_help_section (Result, "Flags", 4)
        end

feature -- Execution

    handle (a_values: CLI_COMMAND_INPUT)
            -- Execute the command using successfully parsed values.
        deferred
        end

feature -- Failure

    Command_error: INTEGER = 1
            -- Command execution failed.

    Invalid_call: INTEGER = 2
            -- Command selection or arguments are invalid.

feature {CLI_APPLICATION} -- Parameter definitions

    parameters: ARRAYED_LIST [CLI_PARAMETER]
            -- Definitions in declaration order; each command owns its list.
        attribute
            create Result.make (Default_capacity)
        end

feature {NONE} -- Parameter definitions

    argument_usage_name (a_parameter: CLI_PARAMETER): STRING_32
        do
            Result := a_parameter.name.to_string_32.twin
            if a_parameter.is_multiple then
                Result.append ("...")
            end
        end

    list_default_text (a_parameter: CLI_PARAMETER): STRING_32
        do
            Result := "["
            if a_parameter.value_type = a_parameter.Integer_type then
                across a_parameter.integer_list_default as value loop
                    if Result.count > 1 then Result.append (", ") end
                    Result.append_string_general (value.out)
                end
            else
                across a_parameter.string_list_default as value loop
                    if Result.count > 1 then Result.append (", ") end
                    Result.append_character ('%"')
                    Result.append (value)
                    Result.append_character ('%"')
                end
            end
            Result.append_character (']')
        end

    parameter_type_name (a_parameter: CLI_PARAMETER): STRING_32
        do
            if a_parameter.value_type = a_parameter.Integer_type then
                Result := "integer"
            elseif a_parameter.value_type = a_parameter.String_type then
                Result := "string"
            else
                Result := "boolean"
            end
        end

    append_help_section (a_text: STRING_32; a_title: READABLE_STRING_32; a_section: INTEGER)
            -- Append a nonempty section to the fresh help buffer.
        local
            l_started, l_matches: BOOLEAN
        do
            across parameters as p loop
                l_matches := (a_section = 1 and p.kind = p.Argument_kind) or
                    (a_section = 2 and p.kind = p.Option_kind and not p.is_optional) or
                    (a_section = 3 and p.kind = p.Option_kind and p.is_optional) or
                    (a_section = 4 and p.kind = p.Flag_kind)
                if l_matches then
                    if not l_started then
                        a_text.append ({STRING_32} "%N" + a_title + ":%N")
                        l_started := True
                    end
                    a_text.append ("  ")
                    if p.kind = p.Argument_kind then
                        a_text.append (p.name + " <" + parameter_type_name (p) + ">")
                    else
                        if p.has_short_name then
                            a_text.append_character ('-')
                            a_text.append_character (p.short_name)
                            a_text.append (", ")
                        end
                        a_text.append ({STRING_32} "--" + p.name)
                        if p.kind = p.Option_kind then
                            a_text.append ({STRING_32} " <" + parameter_type_name (p) + ">")
                        end
                    end
                    a_text.append ({STRING_32} "  " + p.description)
                    if p.is_multiple and p.kind = p.Option_kind then
                        a_text.append (" (repeatable)")
                    end
                    if p.kind /= p.Flag_kind then
                        if not p.is_optional then
                            a_text.append (" (required)")
                        elseif p.is_multiple then
                            a_text.append ({STRING_32} " (default: " + list_default_text (p) + ")")
                        elseif p.value_type = p.Integer_type then
                            a_text.append ({STRING_32} " (default: " + p.integer_default.out + ")")
                        else
                            a_text.append ({STRING_32} " (default: %"" + p.string_default + "%")")
                        end
                    end
                    a_text.append_character ('%N')
                end
            end
        end

    register_parameter (a_parameter: CLI_PARAMETER)
            -- Register a definition with a unique name and valid position.
        require
            unique_name: is_available_parameter_name (a_parameter.name)
            positional_list_last: a_parameter.kind = a_parameter.Argument_kind implies
                not has_positional_list
            required_argument_before_optional: a_parameter.kind = a_parameter.Argument_kind and then not a_parameter.is_optional implies
                not has_optional_argument
        do
            if a_parameter.name.same_string ("help") and then attached automatic_help as l_help and then
                a_parameter /= l_help
            then
                parameters.prune_all (l_help)
                automatic_help := Void
            end
            parameters.extend (a_parameter)
        ensure
            added: has_parameter (a_parameter.name)
            last_definition: parameters.last = a_parameter
        end

feature {NONE} -- Automatic help

    automatic_help: detachable CLI_PARAMETER
            -- Automatically added definition, replaceable by an explicit one.

feature {NONE} -- Capacity

    Default_capacity: INTEGER = 4
            -- Initial capacity of internal collections.

end
