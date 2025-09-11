function CodeBlock(elem)
  local lang = elem.classes[1] or "text"  -- default to "text" if empty
  return pandoc.RawBlock("latex",
    "\\begin{minted}[fontsize=\\footnotesize,bgcolor=lightgray]{"
    .. lang .. "}\n"
    .. elem.text
    .. "\n\\end{minted}"
  )
end
