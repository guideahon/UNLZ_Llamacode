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
    // Speculative decoding activo: hay draft model resuelto. Con MTP, un KV-cache
    // cuantizado (q4_0/q8_0) colapsa el draft acceptance ~a 0 (necesita f16); ver
    // reportes de comunidad sobre Gemma4 QAT+MTP. Forzamos f16 y avisamos.
    const bool specDecoding =
        !ctx.model.draftModelId.isEmpty() && ctx.draftModel.isAvailable;
    applyRuntime(ctx.runtime, ctx.binary, args, result.warnings,
                 result.blockingErrors, specDecoding);

    // Harness env
    for (auto it = ctx.harness.env.begin(); it != ctx.harness.env.end(); ++it)
        env[it.key()] = it.value();

    // Launch overrides (raw, highest priority)
    for (auto it = ctx.launch.envOverrides.begin(); it != ctx.launch.envOverrides.end(); ++it)
        env[it.key()] = it.value();

    // Resolve aliases in extraArgs with pair-aware parsing.
    // NOTA: NO descartar `-np 1` / `--parallel 1`. Algunos forks (p.ej. MTP)
    // tienen n_parallel=auto que abre 4 slots; cada uno reserva su KV-cache del
    // ctx-size completo (262k) → OOM de VRAM y crash 0xC0000409. `-np 1` es
    // necesario para limitar a un slot. Pasar todos los flags tal cual.
    QStringList extraTokens;
    for (const QString &rawArg : ctx.launch.extraArgs) {
        const QStringList tokens = rawArg.trimmed().split(u' ', Qt::SkipEmptyParts);
        for (const QString &t : tokens)
            extraTokens.append(t);
    }
    for (const QString &cur : extraTokens) {
        if (!cur.startsWith(u'-')) { args.append(cur); continue; }
        args.append(ctx.binary.resolveFlag(cur));
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
        if (!draft.isAvailable) {
            warnings.append("Draft model unavailable, speculative decoding disabled.");
        } else {
            addFlag(bin, "--draft-model", draft.absolutePath, args, warnings);
            // Flags de speculative decoding / MTP. Solo se emiten los seteados;
            // vacío/0 = default del binario.
            if (!mp.specType.isEmpty())
                addFlag(bin, "--spec-type", mp.specType, args, warnings);
            if (mp.specDraftNMax > 0)
                addFlag(bin, "--spec-draft-n-max",
                        QString::number(mp.specDraftNMax), args, warnings);
            if (!mp.specDraftNgl.isEmpty())
                addFlag(bin, "--spec-draft-ngl", mp.specDraftNgl, args, warnings);
            if (!mp.specDraftTypeK.isEmpty())
                addFlag(bin, "--spec-draft-type-k", mp.specDraftTypeK, args, warnings);
            if (!mp.specDraftTypeV.isEmpty())
                addFlag(bin, "--spec-draft-type-v", mp.specDraftTypeV, args, warnings);
        }
    }
}

void EffectiveProfileBuilder::applyRuntime(const RuntimePreset &rt,
                                           const LlamaBinary &bin,
                                           QStringList &args,
                                           QStringList &warnings,
                                           QStringList &errors,
                                           bool specDecoding)
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

    if (!rt.cacheType.isEmpty() && rt.cacheType != "f16") {
        if (specDecoding) {
            warnings.append(QStringLiteral(
                "Speculative decoding active: KV cache quant '%1' kills draft "
                "acceptance; forcing f16.").arg(rt.cacheType));
        } else {
            addFlag(bin, "--cache-type-k", rt.cacheType, args, warnings);
        }
    }
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
