/**
 * A small XML reader, enough for MusicXML: elements, attributes and text.
 * No namespaces or DTD handling. Elements named in `skip` are dropped with everything
 * inside them, which keeps large scores (tens of megabytes) manageable.
 */
export interface XmlNode {
  name: string
  attrs: Record<string, string>
  children: XmlNode[]
  text: string
}

const ENTITIES: Record<string, string> = { amp: '&', lt: '<', gt: '>', quot: '"', apos: '\'' }

const decode = (text: string): string => text.includes('&')
  ? text.replace(/&(#x[0-9a-f]+|#\d+|\w+);/gi, (whole, code: string) => {
    if (code.startsWith('#x')) return String.fromCodePoint(parseInt(code.slice(2), 16))
    if (code.startsWith('#')) return String.fromCodePoint(Number(code.slice(1)))
    return ENTITIES[code] ?? whole
  })
  : text

const NO_ATTRS: Record<string, string> = {}

const parseAttrs = (text: string): Record<string, string> => {
  const attrs: Record<string, string> = {}
  for (const match of text.matchAll(/([\w:.-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g))
    attrs[match[1]!] = decode(match[2] ?? match[3] ?? '')

  return attrs
}

export const parseXml = (source: string, skip: ReadonlySet<string> = new Set()): XmlNode => {
  const root: XmlNode = { name: '#document', attrs: NO_ATTRS, children: [], text: '' }
  const stack = [root]
  let skipDepth = 0
  let i = 0
  while (i < source.length) {
    const lt = source.indexOf('<', i)
    if (lt < 0) break
    if (lt > i && skipDepth === 0) {
      const text = source.slice(i, lt).trim()
      if (text) stack.at(-1)!.text += decode(text)
    }
    if (source.startsWith('<!--', lt)) {
      i = source.indexOf('-->', lt) + 3
      continue
    }
    if (source.startsWith('<![CDATA[', lt)) {
      const end = source.indexOf(']]>', lt)
      if (skipDepth === 0) stack.at(-1)!.text += source.slice(lt + 9, end)
      i = end + 3
      continue
    }
    const gt = source.indexOf('>', lt)
    if (gt < 0) break
    i = gt + 1
    const second = source[lt + 1]
    if (second === '?' || second === '!') continue
    if (second === '/') {
      if (skipDepth > 0) skipDepth -= 1
      else if (stack.length > 1) stack.pop()
      continue
    }
    const selfClosing = source[gt - 1] === '/'
    const body = source.slice(lt + 1, selfClosing ? gt - 1 : gt)
    const space = body.search(/\s/)
    const name = space < 0 ? body : body.slice(0, space)
    if (skipDepth > 0 || skip.has(name)) {
      if (!selfClosing) skipDepth += 1
      continue
    }
    const node: XmlNode = {
      name,
      attrs: space < 0 ? NO_ATTRS : parseAttrs(body.slice(space)),
      children: [],
      text: '',
    }
    stack.at(-1)!.children.push(node)
    if (!selfClosing) stack.push(node)
  }
  return root
}

export const child = (node: XmlNode | undefined, name: string): XmlNode | undefined =>
  node?.children.find(c => c.name === name)

export const childrenOf = (node: XmlNode | undefined, name: string): XmlNode[] =>
  node?.children.filter(c => c.name === name) ?? []

export const textOf = (node: XmlNode | undefined, name: string): string | undefined =>
  child(node, name)?.text

export const numberOf = (node: XmlNode | undefined, name: string): number | undefined => {
  const text = textOf(node, name)
  if (text === undefined || text === '') return undefined
  const value = Number(text)
  return Number.isFinite(value) ? value : undefined
}
