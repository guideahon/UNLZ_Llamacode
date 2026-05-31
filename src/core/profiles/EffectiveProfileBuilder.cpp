#include "EffectiveProfileBuilder.h"
#include <QFileInfo>

EffectiveProfile EffectiveProfileBuilder::build(const Context &ctx)
{
    EffectiveProfile result;
    QStringList args;
    QMap<QString, QString> env = ctx.binary.envDefaults;

    // Validate binary
    if (ctx.binary.id.isEmpty()) {
        result.blockingErrors.append("No binary selected.");
        return result;
    }
    if (!QFileInfo::exists(ctx.binary.path)) {
        result.blockingErrors.append(
            QStringLiteral("Binary not found: %1").arg(ctx.binary.path));
        return result;
    }

    applyBackend(ctx.backend, args, env, result.warnings, result.blockingErrors);
    applyModel(ctx.model, ctx.catalogModel, ctx.mmprojModel, ctx.draftModel,
               ctx.binary, args, result.warnings, result.blockingErrors);
    applyRuntime(ctx.runtime, ctx.binary, args, result.warnings, result.blockingErrors);

    // Harness env
    for (auto it = ctx.harness.env.begin(); it != ctx.harness.env.end(); ++it)
        env[it.key()] = it.value();

    // Launch overrides (raw, highest priority)
    for (auto it = ctx.launch.envOverrides.begin(); it != ctx.launch.envOverrides.end(); ++it)
        env[it.key()] = it.value();

    // Resolve aliases in extraArgs; split entries that contain spaces (flag+value on one line)
    for (const QString &rawArg : ctx.launch.extraArgs) {
        const QStringList tokens = rawArg.trimmed().split(u' ', Qt::SkipEmptyParts);
        for (const QString &arg : tokens)
            args.append(arg.startsWith(u'-') ? ctx.binary.resolveFlag(arg) : arg);
    }

    // Asegurar --jinja: necesario para que el server respete chat_template_kwargs
    // (enable_thinking:false) y el tool-calling por template.
    if (!args.contains(QStringLiteral("--jinja")))
        args << QStringLiteral("--jinja");

    result.effectiveArgs = args;
    result.effectiveEnv = env;
    result.binaryPath = ctx.binary.path;
    result.commandLine = QStringLiteral("\"%1\" %2")
                         .arg(ctx.binary.path, args.join(' '));

    return result;
}

void EffectiveProfileBuilder::applyBackend(const BackendProfile &bp,
                                           QStringList &args,
                                           QMap<QString, QString> &env,
                                           QStringList &warnings,
                                           QStringList &errors)
{
    Q_UNUSED(errors)
    args << "--host" << bp.host;
    args << "--port" << QString::number(bp.port);
    args.append(bp.baseArgs);
    for (auto it = bp.envOverrides.begin(); it != bp.envOverrides.end(); ++it)
        env[it.key()] = it.value();
    if (bp.port < 1024)
        warnings.append(QStringLiteral("Port %1 requires admin privileges.").arg(bp.port));
}

void EffectiveProfileBuilder::applyModel(const ModelProfile &mp,
                                         const CatalogModel &model,
                                         const CatalogModel &mmproj,
                                         const CatalogModel &draft,
                                         const LlamaBinary &bin,
                                         QStringList &args,
                                         QStringList &warnings,
                                         QStringList &errors)
{
    if (mp.modelId.isEmpty() || model.id.isEmpty()) {
        errors.append("No model selected.");
        return;
    }
    if (!model.isAvailable) {
        errors.append(QStringLiteral("Model unavailable: %1").arg(model.fileName));
        return;
    }
    args << "--model" << model.absolutePath;

    if (!mp.mmprojId.isEmpty()) {
        if (!mmproj.isAvailable)
            warnings.append("mmproj model unavailable, vision disabled.");
        else
            addFlag(bin, "--mmproj", mmproj.absolutePath, args, warnings);
    }

    if (!mp.draftModelId.isEmpty()) {
        if (!draft.isAvailable)
            warnings.append("Draft model unavailable, speculative decoding disabled.");
        else
            addFlag(bin, "--draft-model", draft.absolutePath, args, warnings);
    }
}

void EffectiveProfileBuilder::applyRuntime(const RuntimePreset &rt,
                                           const LlamaBinary &bin,
                                           QStringList &args,
                                           QStringList &warnings,
                                           QStringList &errors)
{
    Q_UNUSED(errors)
    args << "--ctx-size" << QString::number(rt.ctx);
    args << "--batch-size" << QString::number(rt.batch);
    args << "--ubatch-size" << QString::number(rt.ubatch);

    if (rt.threads > 0)
        args << "--threads" << QString::number(rt.threads);

    if (rt.gpuLayers < 0)
        args << "--n-gpu-layers" << "999";   // -1 = offload all layers
    else
        args << "--n-gpu-layers" << QString::number(rt.gpuLayers);

    if (rt.flashAttention)
        addFlag(bin, "--flash-attn", "on", args, warnings);

    if (!rt.mmap)
        addFlag(bin, "--no-mmap", {}, args, warnings);
    if (rt.mlock)
        addFlag(bin, "--mlock", {}, args, warnings);
    if (rt.contBatching)
        addFlag(bin, "--cont-batching", {}, args, warnings);

    if (rt.parallelSlots > 1)
        args << "--parallel" << QString::number(rt.parallelSlots);

    if (!rt.cacheType.isEmpty() && rt.cacheType != "f16")
        addFlag(bin, "--cache-type-k", rt.cacheType, args, warnings);
}

void EffectiveProfileBuilder::addFlag(const LlamaBinary &bin, const QString &flag,
                                      const QString &value, QStringList &args,
                                      QStringList &warnings, bool required,
                                      QStringList *errors)
{
    const QString resolved = bin.resolveFlag(flag);
    const bool supported = bin.supportedFlags.isEmpty() || bin.supportsFlag(resolved);
    if (!supported) {
        const QString msg = QStringLiteral("Flag %1 not supported by this binary.").arg(flag);
        if (required && errors) errors->append(msg);
        else warnings.append(msg);
        return;
    }
    args.append(resolved);
    if (!value.isEmpty()) args.append(value);
}
