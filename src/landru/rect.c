#include <landru/rect.h>
#include <string.h>

void lrect_Clear_Rect(Rect* rect) {
	rect->top = 0;
	rect->left = 0;
	rect->bottom = 0;
	rect->right = 0;
}

void lrect_Set_Rect(Rect* rect, int16_t left, int16_t top, int16_t right, int16_t bottom) {
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}

void lrect_Max_Rect(Rect* rect) {
	rect->left = -32767;
	rect->top = -32767;
	rect->right = 32767;
	rect->bottom = 32767;
}

void lrect_Copy_Rect(Rect* dst, Rect* src) {
	dst->top = src->top;
	dst->left = src->left;
	dst->bottom = src->bottom;
	dst->right = src->right;
}

bool lrect_Equal_Rect(Rect* rect1, Rect* rect2) {
	return (rect1->bottom == rect2->bottom) && (rect1->right == rect2->right) && (rect1->top == rect2->top) &&
		   (rect1->left == rect2->left);
}

void lrect_Offset_Rect(Rect* rect, int16_t xOffset, int16_t yOffset) {
	rect->left += xOffset;
	rect->right += xOffset;
	rect->top += yOffset;
	rect->bottom += yOffset;
}

void lrect_Origin_Rect(Rect* rect) { lrect_Offset_Rect(rect, -rect->left, -rect->top); }

void lrect_Allign_Rect(Rect* rect, Rect* parent, uint8_t xAlign, uint8_t yAlign) {
	int16_t temp;

	switch (xAlign) {
		case 0:
			rect->left += parent->left;
			rect->right += parent->left;
			break;

		case 1:
			temp =
				(int16_t)(((parent->right - parent->left) - (rect->right - rect->left)) >> 1) + parent->left;
			rect->left += temp;
			rect->right += temp;
			break;

		case 2:
			temp = rect->left;
			rect->left = parent->right - rect->right;
			rect->right = parent->right - temp;
			break;
	}

	switch (yAlign) {
		case 0:
			rect->top += parent->top;
			rect->bottom += parent->top;
			break;

		case 1:
			temp =
				(int16_t)(((parent->bottom - parent->top) - (rect->bottom - rect->top)) >> 1) + parent->top;
			rect->top += temp;
			rect->bottom += temp;
			break;

		case 2:
			temp = rect->top;
			rect->top = parent->bottom - rect->bottom;
			rect->bottom = parent->bottom - temp;
			break;
	}
}

void lrect_Flip_Rect(Rect* rect, Rect* frame, int16_t hflip, int16_t vflip) {
	int16_t old_left, old_top, new_left, new_top;

	if (hflip) {
		old_left = rect->left;
		new_left = frame->left + frame->right - rect->right;
		rect->left = new_left;
		rect->right = new_left + rect->right - old_left;
	}

	if (vflip) {
		old_top = rect->top;
		new_top = frame->top + frame->bottom - rect->bottom;
		rect->top = new_top;
		rect->bottom = new_top + rect->bottom - old_top;
	}
}

void lrect_Inset_Rect(Rect* rect, int16_t offsetX, int16_t offsetY) {
	rect->left += offsetX;
	rect->right -= offsetX;
	rect->top += offsetY;
	rect->bottom -= offsetY;
}

bool lrect_Empty_Rect(Rect* rect) {
	if ((rect->right <= rect->left) || (rect->bottom <= rect->top)) {
		return true;
	}
	return false;
}

bool lrect_Sect_Rect(Rect* a, Rect* b) {
	if (lrect_Empty_Rect(a) || lrect_Empty_Rect(b))
		return false;
	if (b->left >= a->right)
		return false;
	if (b->right <= a->left)
		return false;
	if (b->top >= a->bottom)
		return false;
	if (a->top >= b->bottom)
		return false;
	return true;
}

void lrect_Enclose_Rect(Rect* dstRect, Rect* srcRect) {
	if (lrect_Empty_Rect(dstRect)) {
		lrect_Copy_Rect(dstRect, srcRect);
		return;
	}

	if (lrect_Empty_Rect(srcRect)) {
		return;
	}

	if (srcRect->left < dstRect->left) {
		dstRect->left = srcRect->left;
	}

	if (dstRect->right < srcRect->right) {
		dstRect->right = srcRect->right;
	}

	if (srcRect->top < dstRect->top) {
		dstRect->top = srcRect->top;
	}

	if (dstRect->bottom < srcRect->bottom) {
		dstRect->bottom = srcRect->bottom;
	}
}

bool lrect_Clip_Rect(Rect* dstRect, Rect* clipRect) {
	if (dstRect->left < clipRect->left) {
		dstRect->left = clipRect->left;
	}

	if (clipRect->right < dstRect->right) {
		dstRect->right = clipRect->right;
	}

	if (dstRect->top < clipRect->top) {
		dstRect->top = clipRect->top;
	}

	if (clipRect->bottom < dstRect->bottom) {
		dstRect->bottom = clipRect->bottom;
	}

	if (dstRect->right < dstRect->left) {
		dstRect->right = dstRect->left;
	}

	if (dstRect->bottom < dstRect->top) {
		dstRect->bottom = dstRect->top;
	}

	if (dstRect->left < dstRect->right && dstRect->top < dstRect->bottom) {
		return true;
	}
	return false;
}

void lrect_Clip_Point_To_Rect(Rect* clipRect, int16_t* pointX, int16_t* pointY) {
	if (*pointX < clipRect->left) {
		*pointX = clipRect->left;
	}

	if (clipRect->right < *pointX) {
		*pointX = clipRect->right;
	}

	if (*pointY < clipRect->top) {
		*pointY = clipRect->top;
	}

	if (clipRect->bottom < *pointY) {
		*pointY = clipRect->bottom;
	}
}

bool lrect_Point_In_Rect(Rect* rect, int16_t pointX, int16_t pointY) {
	if (pointX < rect->left || pointX >= rect->right) {
		return false;
	}
	if (pointY < rect->top || pointY >= rect->bottom) {
		return false;
	}
	return true;
}

void lrect_Set_Poly(Poly* p, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
					int16_t x3, int16_t y3) {
	p->x[0] = x0;
	p->y[0] = y0;
	p->x[1] = x1;
	p->y[1] = y1;
	p->x[2] = x2;
	p->y[2] = y2;
	p->x[3] = x3;
	p->y[3] = y3;
}

void lrect_Copy_Poly(Poly* dst, Poly* src) { memcpy(dst, src, sizeof(Poly)); }
