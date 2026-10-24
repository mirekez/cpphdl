"""Verify the exact JSON tuple/list comparison fix, without exempting physics."""
import ast


def verify_json_roundtrip_revision(previous, current):
    old, new = ast.parse(previous), ast.parse(current)
    original = ast.dump(ast.parse('result.get(key) != value', mode='eval').body)
    corrected = ast.parse('result.get(key) != json.loads(json.dumps(value))', mode='eval').body
    matches = 0

    class CorrectComparison(ast.NodeTransformer):
        def visit_Compare(self, node):
            nonlocal matches
            if ast.dump(node) == original:
                matches += 1
                return corrected
            return self.generic_visit(node)

    functions = [node for node in old.body if isinstance(node, ast.FunctionDef) and node.name == 'compare']
    if len(functions) != 1:
        raise ValueError('missing original comparison function')
    CorrectComparison().visit(functions[0])
    if matches != 1 or ast.dump(old) != ast.dump(new):
        raise ValueError('revision changes more than the JSON comparison expression')
