.pragma library

/*
    递归下降表达式解析器：分词 -> 语法树 -> 求值。
    支持 + - * / % ^、括号、一元正负号、sin/cos/tan、log/ln、sqrt 等函数，
    以及 2x、16sin^3(t) 这类省略乘号的写法。
*/

var FUNCTIONS = ["sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
                 "sqrt", "abs", "ln", "log", "log2", "exp", "floor", "ceil", "round",
                 "sign", "min", "max", "pow"]

var CONSTANTS = { pi: Math.PI, e: Math.E }

function normalize(text) {
    return String(text)
        .replace(/×/g, "*")
        .replace(/÷/g, "/")
        .replace(/−/g, "-")
        .replace(/（/g, "(")
        .replace(/）/g, ")")
        .replace(/，/g, ",")
        .replace(/π/g, "pi")
        .replace(/²/g, "^2")
        .replace(/³/g, "^3")
}

function isDigit(ch) {
    return ch >= "0" && ch <= "9"
}

function isLetter(ch) {
    return /[A-Za-z_]/.test(ch)
}

function tokenize(text) {
    var tokens = []
    var index = 0

    while (index < text.length) {
        var ch = text.charAt(index)

        if (ch === " " || ch === "\t" || ch === "\n" || ch === "\r") {
            ++index
            continue
        }

        if (isDigit(ch) || ch === ".") {
            var start = index
            while (index < text.length
                   && (isDigit(text.charAt(index)) || text.charAt(index) === "."))
                ++index

            // 科学计数法：1e5、2.5E-3。只有当 e 后确实跟着数字时才吃进来，
            // 否则 “2e” 应该仍然理解成 2 × e。
            if (index < text.length
                    && (text.charAt(index) === "e" || text.charAt(index) === "E")) {
                var probe = index + 1
                if (probe < text.length
                        && (text.charAt(probe) === "+" || text.charAt(probe) === "-"))
                    ++probe
                if (probe < text.length && isDigit(text.charAt(probe))) {
                    index = probe + 1
                    while (index < text.length && isDigit(text.charAt(index)))
                        ++index
                }
            }

            var raw = text.substring(start, index)
            var value = Number(raw)
            if (isNaN(value))
                throw new Error("数字格式有误：" + raw)
            // 1e999 这类字面量会溢出为 Infinity，提前报错比返回“无上限”更准确
            if (!isFinite(value))
                throw new Error("数字太大，超出计算范围")
            tokens.push({ type: "number", value: value })
            continue
        }

        if (isLetter(ch)) {
            var nameStart = index
            while (index < text.length && /[A-Za-z0-9_]/.test(text.charAt(index)))
                ++index
            tokens.push({ type: "ident", value: text.substring(nameStart, index).toLowerCase() })
            continue
        }

        if ("+-*/^%(),".indexOf(ch) >= 0) {
            tokens.push({ type: "op", value: ch })
            ++index
            continue
        }

        throw new Error("无法识别的字符：" + ch)
    }

    tokens.push({ type: "eof", value: "" })
    return tokens
}

function parse(text) {
    var tokens
    try {
        tokens = tokenize(normalize(text))
    } catch (error) {
        return { ok: false, error: error.message, variables: [] }
    }

    var pos = 0

    function peek() {
        return tokens[pos]
    }

    function advance() {
        return tokens[pos++]
    }

    function matchOp(value) {
        if (peek().type === "op" && peek().value === value) {
            ++pos
            return true
        }
        return false
    }

    function expectOp(value) {
        if (!matchOp(value))
            throw new Error("缺少「" + value + "」")
    }

    function startsOperand() {
        var token = peek()
        return token.type === "number" || token.type === "ident"
               || (token.type === "op" && token.value === "(")
    }

    function parseExpression() {
        var left = parseTerm()
        while (peek().type === "op" && (peek().value === "+" || peek().value === "-")) {
            var op = advance().value
            left = { t: "bin", op: op, a: left, b: parseTerm() }
        }
        return left
    }

    function parseTerm() {
        var left = parseUnary()
        while (true) {
            if (peek().type === "op" && "*/%".indexOf(peek().value) >= 0) {
                var op = advance().value
                left = { t: "bin", op: op, a: left, b: parseUnary() }
            } else if (startsOperand()) {
                // 省略乘号：2x、3(x+1)、4sin(x)
                left = { t: "bin", op: "*", a: left, b: parseUnary() }
            } else {
                return left
            }
        }
    }

    function parseUnary() {
        if (matchOp("-"))
            return { t: "neg", a: parseUnary() }
        if (matchOp("+"))
            return parseUnary()
        return parsePower()
    }

    function parsePower() {
        var base = parsePrimary()
        if (matchOp("^"))
            return { t: "bin", op: "^", a: base, b: parseUnary() }
        return base
    }

    function parsePrimary() {
        var token = advance()

        if (token.type === "number")
            return { t: "num", v: token.value }

        if (token.type === "op" && token.value === "(") {
            var inner = parseExpression()
            expectOp(")")
            return inner
        }

        if (token.type === "ident") {
            var name = token.value

            if (CONSTANTS.hasOwnProperty(name))
                return { t: "num", v: CONSTANTS[name] }

            if (FUNCTIONS.indexOf(name) >= 0) {
                var exponent = null
                if (matchOp("^"))
                    exponent = parseUnary()

                expectOp("(")
                var args = []
                if (!matchOp(")")) {
                    args.push(parseExpression())
                    while (matchOp(","))
                        args.push(parseExpression())
                    expectOp(")")
                }
                if (args.length === 0)
                    throw new Error(name + " 缺少参数")
                return { t: "fn", name: name, args: args, exp: exponent }
            }

            return { t: "var", name: name }
        }

        if (token.type === "eof")
            throw new Error("表达式不完整")
        throw new Error("无法解析：" + token.value)
    }

    try {
        var root = parseExpression()
        if (peek().type !== "eof")
            throw new Error("多余的字符：" + peek().value)
        return { ok: true, root: root, variables: collectVariables(root), error: "" }
    } catch (error) {
        return { ok: false, error: error.message, variables: [] }
    }
}

function collectVariables(node, found) {
    var names = found === undefined ? [] : found
    if (node === null || node === undefined)
        return names

    if (node.t === "var") {
        if (names.indexOf(node.name) < 0)
            names.push(node.name)
    } else if (node.t === "neg") {
        collectVariables(node.a, names)
    } else if (node.t === "bin") {
        collectVariables(node.a, names)
        collectVariables(node.b, names)
    } else if (node.t === "fn") {
        for (var i = 0; i < node.args.length; ++i)
            collectVariables(node.args[i], names)
        collectVariables(node.exp, names)
    }
    return names
}

function applyFunction(name, args) {
    switch (name) {
    case "sin": return Math.sin(args[0])
    case "cos": return Math.cos(args[0])
    case "tan": return Math.tan(args[0])
    case "asin": return Math.asin(args[0])
    case "acos": return Math.acos(args[0])
    case "atan": return Math.atan(args[0])
    case "sinh": return Math.sinh(args[0])
    case "cosh": return Math.cosh(args[0])
    case "tanh": return Math.tanh(args[0])
    case "sqrt": return Math.sqrt(args[0])
    case "abs": return Math.abs(args[0])
    case "ln": return Math.log(args[0])
    case "log": return Math.log(args[0]) / Math.LN10
    case "log2": return Math.log(args[0]) / Math.LN2
    case "exp": return Math.exp(args[0])
    case "floor": return Math.floor(args[0])
    case "ceil": return Math.ceil(args[0])
    case "round": return Math.round(args[0])
    case "sign": return args[0] > 0 ? 1 : (args[0] < 0 ? -1 : 0)
    case "min": return Math.min.apply(null, args)
    case "max": return Math.max.apply(null, args)
    case "pow": return Math.pow(args[0], args[1])
    }
    throw new Error("不支持的函数：" + name)
}

function evaluateNode(node, variables) {
    switch (node.t) {
    case "num":
        return node.v
    case "var":
        var value = variables[node.name]
        if (value === undefined)
            throw new Error("未知变量：" + node.name)
        return value
    case "neg":
        return -evaluateNode(node.a, variables)
    case "bin":
        var left = evaluateNode(node.a, variables)
        var right = evaluateNode(node.b, variables)
        if (node.op === "+")
            return left + right
        if (node.op === "-")
            return left - right
        if (node.op === "*")
            return left * right
        if (node.op === "/")
            return left / right
        if (node.op === "%")
            return left % right
        return Math.pow(left, right)
    case "fn":
        var values = []
        for (var i = 0; i < node.args.length; ++i)
            values.push(evaluateNode(node.args[i], variables))
        var result = applyFunction(node.name, values)
        if (node.exp !== null && node.exp !== undefined)
            result = Math.pow(result, evaluateNode(node.exp, variables))
        return result
    }
    throw new Error("表达式结构异常")
}

// ─────────────────────────────────────────────────────────────
// 精确分数运算
// 用 {n, d} 表示有理数，所有中间结果都做约分与溢出检查；
// 遇到 sin/log/开方等无理运算时返回 null，由调用方退回浮点结果。
// ─────────────────────────────────────────────────────────────
var kFractionLimit = 1e15

function gcd(a, b) {
    a = Math.abs(a)
    b = Math.abs(b)
    while (b > 0) {
        var remainder = a % b
        a = b
        b = remainder
    }
    return a
}

function makeFraction(n, d) {
    if (!isFinite(n) || !isFinite(d) || d === 0)
        return null
    if (Math.abs(n) > kFractionLimit || Math.abs(d) > kFractionLimit)
        return null

    if (d < 0) {
        n = -n
        d = -d
    }

    var divisor = gcd(n, d)
    if (divisor > 1) {
        n = n / divisor
        d = d / divisor
    }
    if (!isFinite(n) || !isFinite(d))
        return null

    return { n: Math.round(n), d: Math.round(d) }
}

function decimalToFraction(value) {
    if (!isFinite(value))
        return null
    if (value === Math.round(value) && Math.abs(value) <= kFractionLimit)
        return { n: value, d: 1 }

    // 最多按 6 位小数还原，足够覆盖计算器输入精度
    var scale = 1e6
    var numerator = Math.round(value * scale)
    if (Math.abs(numerator) > kFractionLimit) {
        scale = 1e3
        numerator = Math.round(value * scale)
    }
    return makeFraction(numerator, scale)
}

function rationalPower(base, exponent) {
    if (exponent < 0)
        return null
    var n = 1
    var d = 1
    for (var i = 0; i < exponent; ++i) {
        n = n * base.n
        d = d * base.d
        if (!isFinite(n) || !isFinite(d)
                || Math.abs(n) > kFractionLimit || Math.abs(d) > kFractionLimit)
            return null
    }
    return makeFraction(n, d)
}

function evalRational(node, variables) {
    if (node === null || node === undefined)
        return null

    switch (node.t) {
    case "num":
        return decimalToFraction(node.v)
    case "var":
        var value = variables[node.name]
        if (value === undefined || !isFinite(value))
            return null
        return decimalToFraction(value)
    case "neg":
        var negated = evalRational(node.a, variables)
        return negated ? makeFraction(-negated.n, negated.d) : null
    case "bin":
        var left = evalRational(node.a, variables)
        if (!left)
            return null
        var right = evalRational(node.b, variables)
        if (!right)
            return null

        if (node.op === "+")
            return makeFraction(left.n * right.d + right.n * left.d, left.d * right.d)
        if (node.op === "-")
            return makeFraction(left.n * right.d - right.n * left.d, left.d * right.d)
        if (node.op === "*")
            return makeFraction(left.n * right.n, left.d * right.d)
        if (node.op === "/") {
            if (right.n === 0)
                return null
            return makeFraction(left.n * right.d, left.d * right.n)
        }
        if (node.op === "%")
            return null
        if (right.d !== 1 || Math.abs(right.n) > 32)
            return null
        if (right.n < 0) {
            if (left.n === 0)
                return null
            var positive = rationalPower(left, -right.n)
            return positive ? makeFraction(positive.d, positive.n) : null
        }
        return rationalPower(left, right.n)
    case "fn":
        var args = []
        for (var i = 0; i < node.args.length; ++i) {
            var argument = evalRational(node.args[i], variables)
            if (!argument)
                return null
            args.push(argument)
        }

        if (node.name === "abs" && args.length === 1)
            return makeFraction(Math.abs(args[0].n), args[0].d)
        if (node.name === "sign" && args.length === 1)
            return { n: args[0].n > 0 ? 1 : (args[0].n < 0 ? -1 : 0), d: 1 }
        if ((node.name === "min" || node.name === "max") && args.length > 0) {
            var best = args[0]
            for (var k = 1; k < args.length; ++k) {
                var compare = args[k].n * best.d - best.n * args[k].d
                if (node.name === "min" ? compare < 0 : compare > 0)
                    best = args[k]
            }
            return best
        }
        if (node.name === "floor" && args.length === 1)
            return { n: Math.floor(args[0].n / args[0].d), d: 1 }
        if (node.name === "ceil" && args.length === 1)
            return { n: Math.ceil(args[0].n / args[0].d), d: 1 }
        if (node.name === "round" && args.length === 1)
            return { n: Math.round(args[0].n / args[0].d), d: 1 }
        return null
    }

    return null
}

// 返回 { ok, value, error }，供计算器直接使用
function evaluate(text, variables) {
    var source = String(text)
    var parsed = parse(source)
    var repaired = false

    if (!parsed.ok) {
        // 半截表达式（5+、sin(、((5）自动补齐后再试一次，不直接报错
        var patched = repair(source)
        if (patched.length > 0 && patched !== source) {
            var patchedResult = parse(patched)
            if (patchedResult.ok) {
                parsed = patchedResult
                repaired = true
            }
        }
    }

    if (!parsed.ok)
        return { ok: false, value: NaN, error: parsed.error, repaired: false, fraction: null }

    try {
        var value = evaluateNode(parsed.root, variables ? variables : {})
        if (typeof value !== "number")
            return { ok: false, value: NaN, error: "无法计算该表达式", repaired: repaired, fraction: null }
        // 分开报告 NaN 与 ±Infinity：前者几乎总是定义域问题，后者是除以 0 或溢出
        if (isNaN(value))
            return { ok: false, value: NaN,
                     error: "结果没有定义，检查一下负数开方或 0 的对数", repaired: repaired, fraction: null }
        if (!isFinite(value))
            return { ok: false, value: NaN,
                     error: "结果没有上限，看看是不是除以 0 或取了 0 的对数", repaired: repaired, fraction: null }

        // 尽力给出精确分数；无理运算（sin/log/sqrt 等）会得到 null
        var fraction = null
        try {
            var exact = evalRational(parsed.root, variables ? variables : {})
            if (exact && exact.d !== 1)
                fraction = exact
        } catch (ignored) {
            fraction = null
        }

        return { ok: true, value: value, error: "", repaired: repaired, fraction: fraction }
    } catch (error) {
        return { ok: false, value: NaN, error: error.message, repaired: repaired, fraction: null }
    }
}

// 把残缺的表达式补成能算的形式：删掉末尾悬空运算符、补齐括号、空括号填 0
function repair(text) {
    var source = normalize(String(text)).trim()
    if (source.length === 0)
        return source

    var dangling = "+-*/^%.,"
    while (source.length > 0 && dangling.indexOf(source.charAt(source.length - 1)) >= 0)
        source = source.substring(0, source.length - 1).trim()

    if (source.length === 0)
        return source

    var opens = 0
    var closes = 0
    for (var i = 0; i < source.length; ++i) {
        if (source.charAt(i) === "(")
            ++opens
        else if (source.charAt(i) === ")")
            ++closes
    }

    // 多余的右括号直接丢掉，缺的左括号稍后补齐
    while (closes > opens && source.charAt(source.length - 1) === ")") {
        source = source.substring(0, source.length - 1)
        --closes
    }

    if (source.length === 0)
        return source

    if (source.charAt(source.length - 1) === "(")
        source += "0"

    var missing = 0
    for (var j = 0; j < source.length; ++j) {
        if (source.charAt(j) === "(")
            ++missing
        else if (source.charAt(j) === ")")
            --missing
    }
    while (missing > 0) {
        source += ")"
        --missing
    }

    // sin() / () 里的空参数视为 0
    return source.replace(/\(\s*\)/g, "(0)")
}

// 把树转成可反复调用的求值函数，绘图时避免重复解析
function compile(text) {
    var parsed = parse(text)
    if (!parsed.ok)
        return { ok: false, error: parsed.error, variables: [], fn: null }

    return {
        ok: true,
        error: "",
        variables: parsed.variables,
        fn: function(variables) {
            return evaluateNode(parsed.root, variables)
        }
    }
}

function formatNumber(value) {
    if (typeof value !== "number" || !isFinite(value))
        return "错误"

    var rounded = Math.round(value * 1e12) / 1e12
    if (Math.abs(rounded) >= 1e12)
        return rounded.toExponential(6)
    return String(rounded)
}

// 计算器显示格式：能精确表示成分数时同时给出分数与小数，例如 “5/6 ≈ 0.833333”
function formatResult(outcome) {
    if (!outcome || !outcome.ok)
        return "错误"

    var text = formatNumber(outcome.value)
    var fraction = outcome.fraction
    if (fraction && fraction.d > 1 && fraction.d <= 100000
            && Math.abs(fraction.n) <= 1e12) {
        return fraction.n + "/" + fraction.d + " ≈ " + text
    }
    return text
}
