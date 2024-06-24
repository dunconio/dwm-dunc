
enum { TypeUndefined, TypeRoot, TypeValue, TypeOperator };
enum { NOOP, OpAnd, OpOr, OpXor, OpNot, OpLeftShift, OpRightShift, OpPlus, OpMinus, OpMultiply, OpDivide };
typedef struct Entity Entity;
struct Entity {
	Entity *parent;
	Entity *child;
	Entity *next;
	double value;
	int type;
};

void
FreeExpressionEntities(Entity *root)
{
	Entity *e = NULL;
	Entity *c = NULL;

	e = root;
	while (e) {
		if ((c = e->child))
			FreeExpressionEntities(c);

		c = e->next;
		free(e);
		e = c;
	}

}


double
EvaluateExpression(Entity *root)
{
	Entity *e = NULL;
	Entity *c = NULL;
	int hasval1 = 0;
	int hasval2 = 0;
	int unary = 0;
	double value1 = 0;
	double value2 = 0;
	unsigned int valueint = 0;
	unsigned int optype = NOOP;

	for (e = root; e; e = e->next) {

		if ((c = e->child))
			e->value = EvaluateExpression(c);

		switch (e->type) {
			case TypeValue:
				if (optype == NOOP) {
					value1 = e->value;
					if (unary) {
						valueint = (unsigned int) value1;
						valueint = ~valueint;
						value1 = (double) valueint;
						unary = 0;
					}
					hasval1 = 1;
				}
				else {
					value2 = e->value;
					if (unary) {
						valueint = (unsigned int) value2;
						valueint = ~valueint;
						value2 = (double) valueint;
						unary = 0;
					}
					hasval2 = 1;
				}
				break;

			case TypeOperator:
				if (e->value == OpNot)
					unary = 1;
				else
					optype = e->value;
				break;

			default:
				continue;
		}
		if (hasval1 && hasval2) {

			switch (optype) {
				case OpPlus:
					value1 += value2;
					break;

				case OpMinus:
					value1 -= value2;
					break;

				case OpMultiply:
					value1 *= value2;
					break;

				case OpDivide:
					value1 /= value2;
					break;

				case OpLeftShift:
					value1 = ((unsigned int) value1 << (unsigned int) value2);
					break;

				case OpRightShift:
					value1 = ((unsigned int) value1 >> (unsigned int) value2);
					break;

				case OpAnd:
					valueint = (unsigned int) value1;
					valueint &= (unsigned int) value2;
					value1 = (double) valueint;
					break;

				case OpOr:
					valueint = (unsigned int) value1;
					valueint |= (unsigned int) value2;
					value1 = (double) valueint;
					break;

				case OpXor:
					valueint = (unsigned int) value1;
					valueint ^= (unsigned int) value2;
					value1 = (double) valueint;
					break;

			}
			optype = NOOP;
			hasval2 = 0;
		}
	}
	return value1;
}

double
ParseExpression(char *expression)
{
	Entity *root = ecalloc(1, sizeof(Entity));
	root->type = TypeRoot;
	Entity *exp = root;
	Entity *e = NULL;
	Entity *c = NULL;
	double value = 0;
	int type = TypeUndefined;
	int openbracket = 0;
	int closebracket = 0;
	char buffer[32];
	int i = 0;

	int iLen = 0;
	while (expression[i]) {
		int iStart = -1;
		for (; expression[i]; i++) {

			if (expression[i] == ' ')
				continue;
			if (expression[i] == '+' || expression[i] == '-' || expression[i] == '*' || expression[i] == '/'
			 || expression[i] == '&' || expression[i] == '|' || expression[i] == '^' || expression[i] == '~') {
				if (iStart == -1) {
					type = TypeOperator;
					if (expression[i] == '+') value = OpPlus;
					if (expression[i] == '-') value = OpMinus;
					if (expression[i] == '*') value = OpMultiply;
					if (expression[i] == '/') value = OpDivide;
					if (expression[i] == '&') value = OpAnd;
					if (expression[i] == '|') value = OpOr;
					if (expression[i] == '^') value = OpXor;
					if (expression[i] == '~') value = OpNot;
					i++;
				}
				else if (iLen == 0)
					iLen = (i - iStart);
				break;
			}
			else if ((expression[i] == '<' || expression[i] == '>') && expression[i] == expression[i+1]) {
				if (iStart == -1) {
					type = TypeOperator;
					value = (expression[i] == '>' ? OpRightShift : OpLeftShift);
					i+=2;
				}
				else if (iLen == 0)
					iLen = (i - iStart);
				break;
			}
			else if (expression[i] == '(') openbracket++;
			else if (expression[i] == ')') {
				 if (++closebracket == 1 && iStart > -1)
					iLen = (i - iStart);
			}
			else if (iStart == -1) iStart = i;
		}

		if (iStart > -1) {
			if (iLen == 0) iLen = (i - iStart);
			e = ecalloc(1, sizeof(Entity));
			e->type = TypeValue;
			strncpy(buffer, expression+iStart, iLen);
			buffer[iLen] = 0;
			if (strstr(buffer,"0x") == buffer)
				e->value = strtol(buffer,NULL,16);
			else if (strstr(buffer,"."))
				e->value = atof(buffer);
			else
				e->value = atoi(buffer);
			iLen = 0;

			if (openbracket) {
				c = ecalloc(1, sizeof(Entity));
				c->type = TypeValue;
				c->value = 0;
				c->parent = exp->parent;
				exp->next = c;
				exp = c;
				for (openbracket--; openbracket > 0; openbracket--) {
					c = ecalloc(1, sizeof(Entity));
					c->type = TypeValue;
					c->value = 0;
					c->parent = exp;
					exp->child = c;
					exp->type = TypeValue;
					exp = c;
				}
				e->parent = exp;
				exp->child = e;
			}
			else {
				e->parent = exp->parent;
				exp->next = e;
			}
			if (closebracket) {
				for (; closebracket > 0; closebracket--) {
					if (e->parent)
						e = e->parent;
				}
			}

			exp = e;
			openbracket = 0;
			type = TypeUndefined;
		}
		else if (type != TypeUndefined) {
			e = ecalloc(1, sizeof(Entity));
			e->type = type;
			e->value = value;
			e->parent = exp->parent;
			exp->next = e;
			exp = e;
			type = TypeUndefined;
			continue;
		}
		else continue;
	}

	value = EvaluateExpression(root);

	FreeExpressionEntities(root);

	return value;
}
