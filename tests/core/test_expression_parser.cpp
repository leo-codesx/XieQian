#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QString>

#include <memory>

namespace {

// QJSEngine 要求先有 QCoreApplication；Catch2 的 main 不创建它
QCoreApplication &testApplication()
{
    static int argc = 1;
    static char appName[] = "xieqian-tests";
    static char *argv[] = {appName, nullptr};
    static QCoreApplication app(argc, argv);
    return app;
}

std::unique_ptr<QJSEngine> createParserEngine()
{
    (void)testApplication();

    const QString path = QStringLiteral(XIEQIAN_SOURCE_DIR "/ui/qml/components/ExpressionParser.js");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }

    // .pragma library 是 QML 导入指令，QJSEngine 直接求值时去掉即可
    QString source = QString::fromUtf8(file.readAll());
    source.remove(QStringLiteral(".pragma library"));

    auto engine = std::make_unique<QJSEngine>();
    const QJSValue result = engine->evaluate(source, path);
    if (result.isError()) {
        return nullptr;
    }
    return engine;
}

QJSValue evaluate(QJSEngine &engine, const QString &expression)
{
    return engine.globalObject().property(QStringLiteral("evaluate")).call({QJSValue(expression)});
}

QJSValue format(QJSEngine &engine, const QJSValue &outcome)
{
    return engine.globalObject().property(QStringLiteral("formatResult")).call({outcome});
}

}  // namespace

TEST_CASE("one third keeps an exact fraction and a six digit approximation", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("1/3"));
    REQUIRE(outcome.property(QStringLiteral("ok")).toBool());

    const QJSValue formatted = format(*engine, outcome);
    REQUIRE(formatted.property(QStringLiteral("exact")).toString() == QStringLiteral("1/3"));
    REQUIRE(formatted.property(QStringLiteral("approx")).toString() == QStringLiteral("≈ 0.333333"));
}

TEST_CASE("floating point noise does not leak into the exact fraction", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("0.1+0.2"));
    REQUIRE(outcome.property(QStringLiteral("ok")).toBool());

    const QJSValue formatted = format(*engine, outcome);
    REQUIRE(formatted.property(QStringLiteral("exact")).toString() == QStringLiteral("3/10"));
}

TEST_CASE("integer results do not show a denominator", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("3/1"));
    REQUIRE(outcome.property(QStringLiteral("ok")).toBool());

    const QJSValue formatted = format(*engine, outcome);
    REQUIRE(formatted.property(QStringLiteral("exact")).toString().isEmpty());
    REQUIRE(formatted.property(QStringLiteral("value")).toString() == QStringLiteral("3"));
}

TEST_CASE("negative fractions keep the sign on the numerator", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("-1/2"));
    REQUIRE(outcome.property(QStringLiteral("ok")).toBool());

    const QJSValue formatted = format(*engine, outcome);
    REQUIRE(formatted.property(QStringLiteral("exact")).toString() == QStringLiteral("-1/2"));
    REQUIRE(formatted.property(QStringLiteral("approx")).toString() == QStringLiteral("≈ -0.5"));
}

TEST_CASE("denominator above the display limit falls back to decimals", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome =
        engine->evaluate(QStringLiteral("({ok:true, value:1/200000, fraction:{n:1,d:200000}})"));
    const QJSValue formatted = format(*engine, outcome);

    REQUIRE(formatted.property(QStringLiteral("exact")).toString().isEmpty());
    REQUIRE(formatted.property(QStringLiteral("value")).toString() == QStringLiteral("0.000005"));
}

TEST_CASE("division by zero is reported as an error", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("1/0"));
    REQUIRE_FALSE(outcome.property(QStringLiteral("ok")).toBool());
    REQUIRE_FALSE(outcome.property(QStringLiteral("error")).toString().isEmpty());
}

TEST_CASE("negative zero displays as zero", "[calculator]")
{
    auto engine = createParserEngine();
    REQUIRE(engine);

    const QJSValue outcome = evaluate(*engine, QStringLiteral("-0"));
    REQUIRE(outcome.property(QStringLiteral("ok")).toBool());

    const QJSValue formatted = format(*engine, outcome);
    REQUIRE(formatted.property(QStringLiteral("exact")).toString().isEmpty());
    REQUIRE(formatted.property(QStringLiteral("value")).toString() == QStringLiteral("0"));
}
