/*
 * manifest_expr — single-pass parser/evaluator for manifest
 * override expressions.
 *
 * Parses + evaluates in one pass. Each evaluation re-scans the
 * expression string; cost is negligible (typical expressions are
 * <30 chars, evaluated once per actor per frame, ~50 actors/frame).
 *
 * Caching the AST per TieScene2dActorEntry is an obvious follow-up if a
 * profile flags the parse-from-string cost; until then, simplicity
 * wins.
 */

#include "tie_remaster/scene2d/manifest_expr.h"

#include "aeron/log.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- evaluator state ---------- */

typedef struct {
	const char* cur; /* read cursor */
	const TieScene2dManifestEvaluationContext* ctx;
	bool error;
	char err_msg[64];
} TieScene2dManifestExpressionEvaluation;

static void TieScene2dExpression_EvalError(TieScene2dManifestExpressionEvaluation* e, const char* msg) {
	if (!e->error) {
		e->error = true;
		snprintf(e->err_msg, sizeof e->err_msg, "%s", msg);
	}
}

static void TieScene2dExpression_SkipWs(TieScene2dManifestExpressionEvaluation* e) {
	while (*e->cur && (*e->cur == ' ' || *e->cur == '\t'))
		e->cur++;
}

static float TieScene2dExpression_LookupVar(TieScene2dManifestExpressionEvaluation* e, const char* name,
											size_t n) {
	const TieScene2dManifestEvaluationContext* c = e->ctx;
#define MATCH(s) (n == sizeof s - 1 && memcmp(name, s, sizeof s - 1) == 0)
	if (MATCH("classic_x"))
		return c->classic_x;
	if (MATCH("classic_y"))
		return c->classic_y;
	if (MATCH("classic_w"))
		return c->classic_w;
	if (MATCH("classic_h"))
		return c->classic_h;
	if (MATCH("sprite_w"))
		return (float)c->sprite_w;
	if (MATCH("sprite_h"))
		return (float)c->sprite_h;
	if (MATCH("viewport_w"))
		return (float)c->viewport_w;
	if (MATCH("viewport_h"))
		return (float)c->viewport_h;
	if (MATCH("region_x"))
		return (float)c->region_x;
	if (MATCH("region_y"))
		return (float)c->region_y;
	if (MATCH("region_w"))
		return (float)c->region_w;
	if (MATCH("region_h"))
		return (float)c->region_h;
	if (MATCH("scale_x"))
		return c->scale_x;
	if (MATCH("scale_y"))
		return c->scale_y;
	if (MATCH("entry_index"))
		return (float)c->entry_index;
#undef MATCH

	char buf[64];
	size_t k = n < sizeof buf - 1 ? n : sizeof buf - 1;
	memcpy(buf, name, k);
	buf[k] = '\0';
	char m[96];
	snprintf(m, sizeof m, "unknown variable '%s'", buf);
	TieScene2dExpression_EvalError(e, m);
	return 0.0f;
}

/* Forward decls for recursive descent. */
static float TieScene2dExpression_ParseExpr(TieScene2dManifestExpressionEvaluation* e);

static float TieScene2dExpression_ParseFactor(TieScene2dManifestExpressionEvaluation* e) {
	TieScene2dExpression_SkipWs(e);
	if (e->error)
		return 0.0f;
	if (!*e->cur) {
		TieScene2dExpression_EvalError(e, "unexpected end of expression");
		return 0.0f;
	}

	if (*e->cur == '(') {
		e->cur++;
		float v = TieScene2dExpression_ParseExpr(e);
		TieScene2dExpression_SkipWs(e);
		if (*e->cur == ')')
			e->cur++;
		else
			TieScene2dExpression_EvalError(e, "missing ')'");
		return v;
	}
	if (*e->cur == '-') {
		e->cur++;
		return -TieScene2dExpression_ParseFactor(e);
	}
	if (*e->cur == '+') {
		e->cur++;
		return TieScene2dExpression_ParseFactor(e);
	}

	/* Number. */
	if (isdigit((unsigned char)*e->cur) || *e->cur == '.') {
		char* end = NULL;
		double v = strtod(e->cur, &end);
		if (end == e->cur) {
			TieScene2dExpression_EvalError(e, "bad number");
			return 0.0f;
		}
		e->cur = end;
		return (float)v;
	}

	/* Identifier. */
	if (isalpha((unsigned char)*e->cur) || *e->cur == '_') {
		const char* start = e->cur;
		while (isalnum((unsigned char)*e->cur) || *e->cur == '_')
			e->cur++;
		size_t n = (size_t)(e->cur - start);
		return TieScene2dExpression_LookupVar(e, start, n);
	}

	TieScene2dExpression_EvalError(e, "unexpected token");
	return 0.0f;
}

static float TieScene2dExpression_ParseTerm(TieScene2dManifestExpressionEvaluation* e) {
	float v = TieScene2dExpression_ParseFactor(e);
	for (;;) {
		TieScene2dExpression_SkipWs(e);
		if (e->error)
			return v;
		char op = *e->cur;
		if (op != '*' && op != '/')
			break;
		e->cur++;
		float rhs = TieScene2dExpression_ParseFactor(e);
		if (op == '*')
			v *= rhs;
		else if (rhs == 0.0f) {
			TieScene2dExpression_EvalError(e, "divide by zero");
			v = 0.0f;
		} else
			v /= rhs;
	}
	return v;
}

static float TieScene2dExpression_ParseExpr(TieScene2dManifestExpressionEvaluation* e) {
	float v = TieScene2dExpression_ParseTerm(e);
	for (;;) {
		TieScene2dExpression_SkipWs(e);
		if (e->error)
			return v;
		char op = *e->cur;
		if (op != '+' && op != '-')
			break;
		e->cur++;
		float rhs = TieScene2dExpression_ParseTerm(e);
		if (op == '+')
			v += rhs;
		else
			v -= rhs;
	}
	return v;
}

/* ---------- public entry points ---------- */

float TieScene2dExpression_Evaluate(const char* expr, const TieScene2dManifestEvaluationContext* ctx) {
	if (!expr || !*expr)
		return 0.0f;
	TieScene2dManifestExpressionEvaluation e = { .cur = expr, .ctx = ctx, .error = false };
	float v = TieScene2dExpression_ParseExpr(&e);
	TieScene2dExpression_SkipWs(&e);
	if (!e.error && *e.cur) {
		TieScene2dExpression_EvalError(&e, "trailing garbage");
	}
	if (e.error) {
		/* One-line warn so the asset team can spot bad syntax in
		 * their manifest. The compositor falls back to 0.0 for the
		 * affected field, which produces an obvious visual glitch
		 * (sprite at origin / collapsed rect) — not a crash. */
		Aeron_LogWarn("tie.manifest", "expr '%s': %s", expr, e.err_msg);
		return 0.0f;
	}
	return v;
}

bool TieScene2dExpression_Rect(const char* expr_x, const char* expr_y, const char* expr_w, const char* expr_h,
							   const TieScene2dManifestEvaluationContext* ctx, float* out_x, float* out_y,
							   float* out_w, float* out_h) {
	bool any = false;
	if (expr_x && *expr_x) {
		*out_x = TieScene2dExpression_Evaluate(expr_x, ctx);
		any = true;
	}
	if (expr_y && *expr_y) {
		*out_y = TieScene2dExpression_Evaluate(expr_y, ctx);
		any = true;
	}
	if (expr_w && *expr_w) {
		*out_w = TieScene2dExpression_Evaluate(expr_w, ctx);
		any = true;
	}
	if (expr_h && *expr_h) {
		*out_h = TieScene2dExpression_Evaluate(expr_h, ctx);
		any = true;
	}
	return any;
}
