note
    description: "Typed parameter values from a successful parse."
    author: "samedit66"
    email: "samedit66@yandex.ru"

class
    CLI_COMMAND_INPUT

create
    make

feature {NONE} -- Initialization

    make
            -- Create an empty set of typed parameter values.
        do
            create integer_values.make (Default_capacity)
            create string_values.make (Default_capacity)
            create boolean_values.make (Default_capacity)
            create integer_list_values.make (Default_capacity)
            create string_list_values.make (Default_capacity)
        end

feature -- Status report

    has_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a value stored for this parameter, including an empty string or list?
        do
            Result := integer_values.has (a_name) or else string_values.has (a_name) or else
                boolean_values.has (a_name) or else integer_list_values.has (a_name) or else
                string_list_values.has (a_name)
        end

    has_integer_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is an integer value stored for `a_name`?
        do
            Result := integer_values.has (a_name)
        end

    has_string_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a string value stored for `a_name`?
        do
            Result := string_values.has (a_name)
        end

    has_boolean_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a Boolean value stored for `a_name`?
        do
            Result := boolean_values.has (a_name)
        end

    has_integer_list_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is an integer list value stored for `a_name`?
        do
            Result := integer_list_values.has (a_name)
        end

    has_string_list_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Is a string list value stored for `a_name`?
        do
            Result := string_list_values.has (a_name)
        end

feature -- Values

    integer_parameter (a_name: READABLE_STRING_32): INTEGER
            -- Stored integer value for `a_name`.
        require
            known_integer_parameter: has_integer_parameter (a_name)
        do
            Result := integer_values.item (a_name)
        end

    string_parameter (a_name: READABLE_STRING_32): STRING_32
            -- A copy of the stored string.
        require
            known_string_parameter: has_string_parameter (a_name)
        do
            check attached string_values.item (a_name) as l_value then
                Result := l_value.twin
            end
        end

    boolean_parameter (a_name: READABLE_STRING_32): BOOLEAN
            -- Stored Boolean value for `a_name`.
        require
            known_boolean_parameter: has_boolean_parameter (a_name)
        do
            Result := boolean_values.item (a_name)
        end

    integer_list_parameter (a_name: READABLE_STRING_32): ARRAYED_LIST [INTEGER]
            -- A copy of the list.
        require
            known_list_parameter: has_integer_list_parameter (a_name)
        do
            create Result.make (Default_capacity)
            check attached integer_list_values.item (a_name) as items then
                across items as value loop
                    Result.extend (value)
                end
            end
        end

    string_list_parameter (a_name: READABLE_STRING_32): ARRAYED_LIST [STRING_32]
            -- A copy of the list, including its strings.
        require
            known_list_parameter: has_string_list_parameter (a_name)
        do
            create Result.make (Default_capacity)
            check attached string_list_values.item (a_name) as items then
                across items as value loop
                    Result.extend (value.twin)
                end
            end
        end

feature {CLI_PARSER} -- Population availability

    can_put_integer (a_name: READABLE_STRING_32): BOOLEAN
            -- Can this name store a value of this type without conflicting with another type?
        do
            Result := not has_string_parameter (a_name) and not has_boolean_parameter (a_name) and
                not has_integer_list_parameter (a_name) and not has_string_list_parameter (a_name)
        end

    can_put_string (a_name: READABLE_STRING_32): BOOLEAN
            -- Can this name store a value of this type without conflicting with another type?
        do
            Result := not has_integer_parameter (a_name) and not has_boolean_parameter (a_name) and
                not has_integer_list_parameter (a_name) and not has_string_list_parameter (a_name)
        end

    can_put_boolean (a_name: READABLE_STRING_32): BOOLEAN
            -- Can this name store a value of this type without conflicting with another type?
        do
            Result := not has_integer_parameter (a_name) and not has_string_parameter (a_name) and
                not has_integer_list_parameter (a_name) and not has_string_list_parameter (a_name)
        end

    can_put_integer_list (a_name: READABLE_STRING_32): BOOLEAN
            -- Can this name store a value of this type without conflicting with another type?
        do
            Result := not has_integer_parameter (a_name) and not has_string_parameter (a_name) and
                not has_boolean_parameter (a_name) and not has_string_list_parameter (a_name)
        end

    can_put_string_list (a_name: READABLE_STRING_32): BOOLEAN
            -- Can this name store a value of this type without conflicting with another type?
        do
            Result := not has_integer_parameter (a_name) and not has_string_parameter (a_name) and
                not has_boolean_parameter (a_name) and not has_integer_list_parameter (a_name)
        end

feature {CLI_PARSER} -- Population

    put_integer (a_name: READABLE_STRING_32; a_value: INTEGER)
            -- Store an integer value for `a_name`.
        require
            consistent_type: can_put_integer (a_name)
        do
            integer_values.force (a_value, a_name.to_string_32.twin)
        ensure
            stored: has_integer_parameter (a_name) and then integer_parameter (a_name) = a_value
        end

    put_string (a_name, a_value: READABLE_STRING_32)
            -- Store a copy of `a_value` for `a_name`.
        require
            consistent_type: can_put_string (a_name)
        do
            string_values.force (a_value.to_string_32.twin, a_name.to_string_32.twin)
        ensure
            stored: has_string_parameter (a_name) and then string_parameter (a_name).same_string (a_value)
        end

    put_boolean (a_name: READABLE_STRING_32; a_value: BOOLEAN)
            -- Store a Boolean value for `a_name`.
        require
            consistent_type: can_put_boolean (a_name)
        do
            boolean_values.force (a_value, a_name.to_string_32.twin)
        ensure
            stored: has_boolean_parameter (a_name) and then boolean_parameter (a_name) = a_value
        end

    put_integer_list (a_name: READABLE_STRING_32; a_values: ITERABLE [INTEGER])
            -- Replace the whole list, copying caller-owned values.
        require
            consistent_type: can_put_integer_list (a_name)
        local
            items: ARRAYED_LIST [INTEGER]
        do
            create items.make (Default_capacity)
            across a_values as value loop
                items.extend (value)
            end
            integer_list_values.force (items, a_name.to_string_32.twin)
        end

    append_integer (a_name: READABLE_STRING_32; a_value: INTEGER)
            -- Append `a_value` to the stored integer list for `a_name`.
        require
            known_list_parameter: has_integer_list_parameter (a_name)
        do
            check attached integer_list_values.item (a_name) as items then
                items.extend (a_value)
            end
        end

    put_string_list (a_name: READABLE_STRING_32; a_values: ITERABLE [READABLE_STRING_32])
            -- Replace the whole list, copying caller-owned values.
        require
            consistent_type: can_put_string_list (a_name)
        local
            items: ARRAYED_LIST [STRING_32]
        do
            create items.make (Default_capacity)
            across a_values as value loop
                items.extend (value.to_string_32.twin)
            end
            string_list_values.force (items, a_name.to_string_32.twin)
        end

    append_string (a_name: READABLE_STRING_32; a_value: READABLE_STRING_32)
            -- Append a copy of `a_value` to the stored string list for `a_name`.
        require
            known_list_parameter: has_string_list_parameter (a_name)
        do
            check attached string_list_values.item (a_name) as items then
                items.extend (a_value.to_string_32.twin)
            end
        end

feature {NONE} -- Storage

    integer_values: HASH_TABLE [INTEGER, STRING_32]

    string_values: HASH_TABLE [STRING_32, STRING_32]

    boolean_values: HASH_TABLE [BOOLEAN, STRING_32]

    integer_list_values: HASH_TABLE [ARRAYED_LIST [INTEGER], STRING_32]

    string_list_values: HASH_TABLE [ARRAYED_LIST [STRING_32], STRING_32]

feature {NONE} -- Capacity

    Default_capacity: INTEGER = 4
            -- Initial capacity of internal collections.

end
