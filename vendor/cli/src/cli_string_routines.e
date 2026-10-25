note
    description: "Parameter-name validation and explicit splitting of string values."
    author: "samedit66"
    email: "samedit66@yandex.ru"

class
    CLI_STRING_ROUTINES

feature -- Validation

    is_valid_parameter_name (a_name: READABLE_STRING_32): BOOLEAN
            -- Is this a nonempty name without spaces, equals signs, or a leading dash?
        do
            Result := not a_name.is_empty and then not a_name.has (' ') and then
                not a_name.has ('=') and then not a_name.starts_with ("-")
        end

feature -- Splitting

    split (a_value: READABLE_STRING_32; a_separator: CHARACTER_32): ARRAYED_LIST [STRING_32]
            -- Parts separated by `a_separator`, preserving empty parts and whitespace.
            -- Quotes and escape characters have no special meaning.
        local
            l_part: STRING_32
            i: INTEGER
        do
            create Result.make (1)
            create l_part.make_empty
            from i := 1 until i > a_value.count loop
                if a_value [i] = a_separator then
                    Result.extend (l_part)
                    create l_part.make_empty
                else
                    l_part.append_character (a_value [i])
                end
                i := i + 1
            end
            Result.extend (l_part)
        ensure
            at_least_one_part: not Result.is_empty
        end

end
