function CodeBlock(elem)
  local lang = elem.classes[1] or "text"  -- default to "text" if empty
  -- Pygments has no Mermaid lexer; preserve diagram source as plain text.
  if lang == "mermaid" then lang = "text" end
  return pandoc.RawBlock("latex",
    "\\begin{minted}[fontsize=\\footnotesize,bgcolor=lightgray]{"
    .. lang .. "}\n"
    .. elem.text
    .. "\n\\end{minted}"
  )
end
