note
	description: "CLI pathname tree preserving every entry, duplicate and original pathname."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARC_LIST_TREE

create
	make

feature {NONE} -- Initialization

	make (a_component: READABLE_STRING_32)
			-- Create a node with an owned component label.
		do
			create component.make_from_string (a_component)
			create children.make (0)
			create entries.make (0)
			create child_index.make (0)
		end

feature -- Commands

	add (entry: ARCHIVE_ENTRY)
			-- Insert slash-separated components literally and retain the original entry.
		local
			node, next_node: ARC_LIST_TREE
			parts: LIST [IMMUTABLE_STRING_32]
		do
			node := Current
			parts := entry.pathname.split ('/')
			if entry.kind = entry.Directory and then parts.count > 1 and then parts.last.is_empty then
				parts.finish
				parts.remove
			end

			across parts as part loop
				if attached node.child (part) as existing then
					next_node := existing
				else
					create next_node.make (part)
					node.children.extend (next_node)
					node.child_index.force (next_node, part)
				end

				node := next_node
			end

			node.entries.extend (entry)
		end

	print_tree (prefix: READABLE_STRING_32)
			-- Print nodes in first-seen order and each stored entry with its original pathname.
		local
			display: ARC_ENTRY_DISPLAY
			label, next_prefix: STRING_32
		do
			create display
			label := display.escaped (component)
			if label.is_empty then
				label := "<empty component>"
			end
			display.put_line (prefix + label)
			next_prefix := prefix + {STRING_32} "  "

			across entries as entry loop
				display.put_line (next_prefix + {STRING_32} "[" + display.kind_name (entry) + "] " + display.escaped (entry.pathname))
			end

			across children as node loop
				node.print_tree (next_prefix)
			end
		end

feature {ARC_LIST_TREE} -- Tree construction

	child (name: IMMUTABLE_STRING_32): detachable ARC_LIST_TREE
			-- Existing node for the literal component, if present.
		do
			Result := child_index.item (name)
		end

	component: IMMUTABLE_STRING_32
			-- Literal path component; empty components are preserved.

	entries: ARRAYED_LIST [ARCHIVE_ENTRY]
			-- All entries at this node, including duplicates and conflicting types.

feature {ARC_LIST_TREE} -- Storage

	children: ARRAYED_LIST [ARC_LIST_TREE]
			-- Child nodes in first-seen order.

	child_index: HASH_TABLE [ARC_LIST_TREE, IMMUTABLE_STRING_32]
			-- Lookup by immutable literal component.

end
