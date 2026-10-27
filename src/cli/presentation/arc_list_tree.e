note
	description: "CLI pathname tree preserving every entry, duplicate and original pathname."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_LIST_TREE

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

	add (pathname: READABLE_STRING_32; kind: READABLE_STRING_32; directory: BOOLEAN)
			-- Insert slash-separated components literally and retain copied entry labels.
		local
			node, next_node: ARC_LIST_TREE
			parts: LIST [STRING_32]
		do
			node := Current
			parts := pathname.to_string_32.split ('/')
			if directory and then parts.count > 1 and then parts.last.is_empty then
				parts.finish
				parts.remove
			end

			across parts as part loop
				if attached node.child (create {IMMUTABLE_STRING_32}.make_from_string (part)) as existing then
					next_node := existing
				else
					create next_node.make (part)
					node.children.extend (next_node)
					node.child_index.force (next_node, create {IMMUTABLE_STRING_32}.make_from_string (part))
				end

				node := next_node
			end

			node.entries.extend ([create {IMMUTABLE_STRING_32}.make_from_string (pathname), create {IMMUTABLE_STRING_32}.make_from_string (kind)])
		end

	print_tree (prefix: READABLE_STRING_32)
			-- Print nodes in first-seen order and each stored entry with its original pathname.
		local
			label, next_prefix: STRING_32
		do
			label := {ARC_ENTRY_DISPLAY}.escaped (component)
			if label.is_empty then
				label := "<empty component>"
			end
			{ARC_ENTRY_DISPLAY}.put_line (prefix + label)
			next_prefix := prefix + {STRING_32} "  "

			across entries as entry loop
				{ARC_ENTRY_DISPLAY}.put_line (next_prefix + {STRING_32} "[" + entry.kind + "] " + {ARC_ENTRY_DISPLAY}.escaped (entry.pathname))
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

	entries: ARRAYED_LIST [TUPLE [pathname: IMMUTABLE_STRING_32; kind: IMMUTABLE_STRING_32]]
			-- All entries at this node, including duplicates and conflicting types.

feature {ARC_LIST_TREE} -- Storage

	children: ARRAYED_LIST [ARC_LIST_TREE]
			-- Child nodes in first-seen order.

	child_index: HASH_TABLE [ARC_LIST_TREE, IMMUTABLE_STRING_32]
			-- Lookup by immutable literal component.

end
