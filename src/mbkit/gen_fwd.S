/* 本文件由 gen_fwd.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物）。
 * 重新生成：python gen_fwd.py <清单.json>
 */
	.text
	.globl _JsBindGetter
_JsBindGetter:
	movl	_mbk_slots+0, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+0, %eax
1:
	jmp	*%eax
	.globl _JsBindSetter
_JsBindSetter:
	movl	_mbk_slots+4, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+4, %eax
1:
	jmp	*%eax
	.globl _jsArg
_jsArg:
	movl	_mbk_slots+8, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+8, %eax
1:
	jmp	*%eax
	.globl _jsArgCount
_jsArgCount:
	movl	_mbk_slots+12, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+12, %eax
1:
	jmp	*%eax
	.globl _jsArgType
_jsArgType:
	movl	_mbk_slots+16, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+16, %eax
1:
	jmp	*%eax
	.globl _jsArrayBuffer
_jsArrayBuffer:
	movl	_mbk_slots+20, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+20, %eax
1:
	jmp	*%eax
	.globl _jsBindFunction
_jsBindFunction:
	movl	_mbk_slots+24, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+24, %eax
1:
	jmp	*%eax
	.globl _jsBoolean
_jsBoolean:
	movl	_mbk_slots+28, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+28, %eax
1:
	jmp	*%eax
	.globl _jsCall
_jsCall:
	movl	_mbk_slots+32, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+32, %eax
1:
	jmp	*%eax
	.globl _jsCallGlobal
_jsCallGlobal:
	movl	_mbk_slots+36, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+36, %eax
1:
	jmp	*%eax
	.globl _jsDeleteObjectProp
_jsDeleteObjectProp:
	movl	_mbk_slots+40, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+40, %eax
1:
	jmp	*%eax
	.globl _jsDouble
_jsDouble:
	movl	_mbk_slots+44, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+44, %eax
1:
	jmp	*%eax
	.globl _jsEmptyArray
_jsEmptyArray:
	movl	_mbk_slots+48, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+48, %eax
1:
	jmp	*%eax
	.globl _jsEmptyObject
_jsEmptyObject:
	movl	_mbk_slots+52, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+52, %eax
1:
	jmp	*%eax
	.globl _jsEvalExW
_jsEvalExW:
	movl	_mbk_slots+56, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+56, %eax
1:
	jmp	*%eax
	.globl _jsEvalW
_jsEvalW:
	movl	_mbk_slots+60, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+60, %eax
1:
	jmp	*%eax
	.globl _jsFalse
_jsFalse:
	movl	_mbk_slots+64, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+64, %eax
1:
	jmp	*%eax
	.globl _jsFloat
_jsFloat:
	movl	_mbk_slots+68, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+68, %eax
1:
	jmp	*%eax
	.globl _jsFunction
_jsFunction:
	movl	_mbk_slots+72, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+72, %eax
1:
	jmp	*%eax
	.globl _jsGC
_jsGC:
	movl	_mbk_slots+76, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+76, %eax
1:
	jmp	*%eax
	.globl _jsGet
_jsGet:
	movl	_mbk_slots+80, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+80, %eax
1:
	jmp	*%eax
	.globl _jsGetArrayBuffer
_jsGetArrayBuffer:
	movl	_mbk_slots+84, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+84, %eax
1:
	jmp	*%eax
	.globl _jsGetAt
_jsGetAt:
	movl	_mbk_slots+88, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+88, %eax
1:
	jmp	*%eax
	.globl _jsGetData
_jsGetData:
	movl	_mbk_slots+92, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+92, %eax
1:
	jmp	*%eax
	.globl _jsGetGlobal
_jsGetGlobal:
	movl	_mbk_slots+96, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+96, %eax
1:
	jmp	*%eax
	.globl _jsGetKeys
_jsGetKeys:
	movl	_mbk_slots+100, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+100, %eax
1:
	jmp	*%eax
	.globl _jsGetLastErrorIfException
_jsGetLastErrorIfException:
	movl	_mbk_slots+104, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+104, %eax
1:
	jmp	*%eax
	.globl _jsGetLength
_jsGetLength:
	movl	_mbk_slots+108, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+108, %eax
1:
	jmp	*%eax
	.globl _jsGetWebView
_jsGetWebView:
	movl	_mbk_slots+112, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+112, %eax
1:
	jmp	*%eax
	.globl _jsGlobalObject
_jsGlobalObject:
	movl	_mbk_slots+116, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+116, %eax
1:
	jmp	*%eax
	.globl _jsInt
_jsInt:
	movl	_mbk_slots+120, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+120, %eax
1:
	jmp	*%eax
	.globl _jsIsArray
_jsIsArray:
	movl	_mbk_slots+124, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+124, %eax
1:
	jmp	*%eax
	.globl _jsIsBoolean
_jsIsBoolean:
	movl	_mbk_slots+128, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+128, %eax
1:
	jmp	*%eax
	.globl _jsIsFalse
_jsIsFalse:
	movl	_mbk_slots+132, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+132, %eax
1:
	jmp	*%eax
	.globl _jsIsFunction
_jsIsFunction:
	movl	_mbk_slots+136, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+136, %eax
1:
	jmp	*%eax
	.globl _jsIsJsValueValid
_jsIsJsValueValid:
	movl	_mbk_slots+140, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+140, %eax
1:
	jmp	*%eax
	.globl _jsIsNull
_jsIsNull:
	movl	_mbk_slots+144, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+144, %eax
1:
	jmp	*%eax
	.globl _jsIsNumber
_jsIsNumber:
	movl	_mbk_slots+148, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+148, %eax
1:
	jmp	*%eax
	.globl _jsIsObject
_jsIsObject:
	movl	_mbk_slots+152, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+152, %eax
1:
	jmp	*%eax
	.globl _jsIsString
_jsIsString:
	movl	_mbk_slots+156, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+156, %eax
1:
	jmp	*%eax
	.globl _jsIsTrue
_jsIsTrue:
	movl	_mbk_slots+160, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+160, %eax
1:
	jmp	*%eax
	.globl _jsIsUndefined
_jsIsUndefined:
	movl	_mbk_slots+164, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+164, %eax
1:
	jmp	*%eax
	.globl _jsIsValidExecState
_jsIsValidExecState:
	movl	_mbk_slots+168, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+168, %eax
1:
	jmp	*%eax
	.globl _jsNull
_jsNull:
	movl	_mbk_slots+172, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+172, %eax
1:
	jmp	*%eax
	.globl _jsObject
_jsObject:
	movl	_mbk_slots+176, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+176, %eax
1:
	jmp	*%eax
	.globl _jsSet
_jsSet:
	movl	_mbk_slots+180, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+180, %eax
1:
	jmp	*%eax
	.globl _jsSetAt
_jsSetAt:
	movl	_mbk_slots+184, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+184, %eax
1:
	jmp	*%eax
	.globl _jsSetGlobal
_jsSetGlobal:
	movl	_mbk_slots+188, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+188, %eax
1:
	jmp	*%eax
	.globl _jsSetLength
_jsSetLength:
	movl	_mbk_slots+192, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+192, %eax
1:
	jmp	*%eax
	.globl _jsStringW
_jsStringW:
	movl	_mbk_slots+196, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+196, %eax
1:
	jmp	*%eax
	.globl _jsThrowException
_jsThrowException:
	movl	_mbk_slots+200, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+200, %eax
1:
	jmp	*%eax
	.globl _jsToBoolean
_jsToBoolean:
	movl	_mbk_slots+204, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+204, %eax
1:
	jmp	*%eax
	.globl _jsToDouble
_jsToDouble:
	movl	_mbk_slots+208, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+208, %eax
1:
	jmp	*%eax
	.globl _jsToFloat
_jsToFloat:
	movl	_mbk_slots+212, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+212, %eax
1:
	jmp	*%eax
	.globl _jsToInt
_jsToInt:
	movl	_mbk_slots+216, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+216, %eax
1:
	jmp	*%eax
	.globl _jsToStringW
_jsToStringW:
	movl	_mbk_slots+220, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+220, %eax
1:
	jmp	*%eax
	.globl _jsToTempStringW
_jsToTempStringW:
	movl	_mbk_slots+224, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+224, %eax
1:
	jmp	*%eax
	.globl _jsTrue
_jsTrue:
	movl	_mbk_slots+228, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+228, %eax
1:
	jmp	*%eax
	.globl _jsTypeOf
_jsTypeOf:
	movl	_mbk_slots+232, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+232, %eax
1:
	jmp	*%eax
	.globl _jsUndefined
_jsUndefined:
	movl	_mbk_slots+236, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+236, %eax
1:
	jmp	*%eax
	.globl _wkeAddDirtyArea
_wkeAddDirtyArea:
	movl	_mbk_slots+240, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+240, %eax
1:
	jmp	*%eax
	.globl _wkeAddPluginDirectory
_wkeAddPluginDirectory:
	movl	_mbk_slots+244, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+244, %eax
1:
	jmp	*%eax
	.globl _wkeCanGoBack
_wkeCanGoBack:
	movl	_mbk_slots+248, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+248, %eax
1:
	jmp	*%eax
	.globl _wkeCanGoForward
_wkeCanGoForward:
	movl	_mbk_slots+252, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+252, %eax
1:
	jmp	*%eax
	.globl _wkeCreateStringW
_wkeCreateStringW:
	movl	_mbk_slots+256, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+256, %eax
1:
	jmp	*%eax
	.globl _wkeCreateWebView
_wkeCreateWebView:
	movl	_mbk_slots+260, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+260, %eax
1:
	jmp	*%eax
	.globl _wkeCreateWebWindow
_wkeCreateWebWindow:
	movl	_mbk_slots+264, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+264, %eax
1:
	jmp	*%eax
	.globl _wkeDeleteString
_wkeDeleteString:
	movl	_mbk_slots+268, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+268, %eax
1:
	jmp	*%eax
	.globl _wkeDestroyWebView
_wkeDestroyWebView:
	movl	_mbk_slots+272, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+272, %eax
1:
	jmp	*%eax
	.globl _wkeDestroyWebWindow
_wkeDestroyWebWindow:
	movl	_mbk_slots+276, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+276, %eax
1:
	jmp	*%eax
	.globl _wkeEditorCopy
_wkeEditorCopy:
	movl	_mbk_slots+280, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+280, %eax
1:
	jmp	*%eax
	.globl _wkeEditorCut
_wkeEditorCut:
	movl	_mbk_slots+284, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+284, %eax
1:
	jmp	*%eax
	.globl _wkeEditorDelete
_wkeEditorDelete:
	movl	_mbk_slots+288, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+288, %eax
1:
	jmp	*%eax
	.globl _wkeEditorPaste
_wkeEditorPaste:
	movl	_mbk_slots+292, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+292, %eax
1:
	jmp	*%eax
	.globl _wkeEditorRedo
_wkeEditorRedo:
	movl	_mbk_slots+296, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+296, %eax
1:
	jmp	*%eax
	.globl _wkeEditorSelectAll
_wkeEditorSelectAll:
	movl	_mbk_slots+300, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+300, %eax
1:
	jmp	*%eax
	.globl _wkeEditorUnSelect
_wkeEditorUnSelect:
	movl	_mbk_slots+304, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+304, %eax
1:
	jmp	*%eax
	.globl _wkeEditorUndo
_wkeEditorUndo:
	movl	_mbk_slots+308, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+308, %eax
1:
	jmp	*%eax
	.globl _wkeEnableWindow
_wkeEnableWindow:
	movl	_mbk_slots+312, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+312, %eax
1:
	jmp	*%eax
	.globl _wkeFinalize
_wkeFinalize:
	movl	_mbk_slots+316, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+316, %eax
1:
	jmp	*%eax
	.globl _wkeFireContextMenuEvent
_wkeFireContextMenuEvent:
	movl	_mbk_slots+320, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+320, %eax
1:
	jmp	*%eax
	.globl _wkeFireKeyDownEvent
_wkeFireKeyDownEvent:
	movl	_mbk_slots+324, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+324, %eax
1:
	jmp	*%eax
	.globl _wkeFireKeyPressEvent
_wkeFireKeyPressEvent:
	movl	_mbk_slots+328, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+328, %eax
1:
	jmp	*%eax
	.globl _wkeFireKeyUpEvent
_wkeFireKeyUpEvent:
	movl	_mbk_slots+332, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+332, %eax
1:
	jmp	*%eax
	.globl _wkeFireMouseEvent
_wkeFireMouseEvent:
	movl	_mbk_slots+336, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+336, %eax
1:
	jmp	*%eax
	.globl _wkeFireMouseWheelEvent
_wkeFireMouseWheelEvent:
	movl	_mbk_slots+340, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+340, %eax
1:
	jmp	*%eax
	.globl _wkeFireWindowsMessage
_wkeFireWindowsMessage:
	movl	_mbk_slots+344, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+344, %eax
1:
	jmp	*%eax
	.globl _wkeGC
_wkeGC:
	movl	_mbk_slots+348, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+348, %eax
1:
	jmp	*%eax
	.globl _wkeGetBlinkMainThreadIsolate
_wkeGetBlinkMainThreadIsolate:
	movl	_mbk_slots+352, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+352, %eax
1:
	jmp	*%eax
	.globl _wkeGetCaretRect
_wkeGetCaretRect:
	movl	_mbk_slots+356, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+356, %eax
1:
	jmp	*%eax
	.globl _wkeGetContentHeight
_wkeGetContentHeight:
	movl	_mbk_slots+360, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+360, %eax
1:
	jmp	*%eax
	.globl _wkeGetContentWidth
_wkeGetContentWidth:
	movl	_mbk_slots+364, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+364, %eax
1:
	jmp	*%eax
	.globl _wkeGetCookie
_wkeGetCookie:
	movl	_mbk_slots+368, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+368, %eax
1:
	jmp	*%eax
	.globl _wkeGetCookieW
_wkeGetCookieW:
	movl	_mbk_slots+372, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+372, %eax
1:
	jmp	*%eax
	.globl _wkeGetCursorInfoType
_wkeGetCursorInfoType:
	movl	_mbk_slots+376, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+376, %eax
1:
	jmp	*%eax
	.globl _wkeGetFrameUrl
_wkeGetFrameUrl:
	movl	_mbk_slots+380, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+380, %eax
1:
	jmp	*%eax
	.globl _wkeGetGlobalExecByFrame
_wkeGetGlobalExecByFrame:
	movl	_mbk_slots+384, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+384, %eax
1:
	jmp	*%eax
	.globl _wkeGetHeight
_wkeGetHeight:
	movl	_mbk_slots+388, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+388, %eax
1:
	jmp	*%eax
	.globl _wkeGetHostHWND
_wkeGetHostHWND:
	movl	_mbk_slots+392, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+392, %eax
1:
	jmp	*%eax
	.globl _wkeGetMediaVolume
_wkeGetMediaVolume:
	movl	_mbk_slots+396, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+396, %eax
1:
	jmp	*%eax
	.globl _wkeGetName
_wkeGetName:
	movl	_mbk_slots+400, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+400, %eax
1:
	jmp	*%eax
	.globl _wkeGetSource
_wkeGetSource:
	movl	_mbk_slots+404, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+404, %eax
1:
	jmp	*%eax
	.globl _wkeGetString
_wkeGetString:
	movl	_mbk_slots+408, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+408, %eax
1:
	jmp	*%eax
	.globl _wkeGetStringW
_wkeGetStringW:
	movl	_mbk_slots+412, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+412, %eax
1:
	jmp	*%eax
	.globl _wkeGetTitle
_wkeGetTitle:
	movl	_mbk_slots+416, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+416, %eax
1:
	jmp	*%eax
	.globl _wkeGetTitleW
_wkeGetTitleW:
	movl	_mbk_slots+420, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+420, %eax
1:
	jmp	*%eax
	.globl _wkeGetURL
_wkeGetURL:
	movl	_mbk_slots+424, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+424, %eax
1:
	jmp	*%eax
	.globl _wkeGetUserAgent
_wkeGetUserAgent:
	movl	_mbk_slots+428, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+428, %eax
1:
	jmp	*%eax
	.globl _wkeGetUserKeyValue
_wkeGetUserKeyValue:
	movl	_mbk_slots+432, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+432, %eax
1:
	jmp	*%eax
	.globl _wkeGetVersion
_wkeGetVersion:
	movl	_mbk_slots+436, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+436, %eax
1:
	jmp	*%eax
	.globl _wkeGetVersionString
_wkeGetVersionString:
	movl	_mbk_slots+440, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+440, %eax
1:
	jmp	*%eax
	.globl _wkeGetViewDC
_wkeGetViewDC:
	movl	_mbk_slots+444, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+444, %eax
1:
	jmp	*%eax
	.globl _wkeGetWebView
_wkeGetWebView:
	movl	_mbk_slots+448, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+448, %eax
1:
	jmp	*%eax
	.globl _wkeGetWebViewForCurrentContext
_wkeGetWebViewForCurrentContext:
	movl	_mbk_slots+452, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+452, %eax
1:
	jmp	*%eax
	.globl _wkeGetWidth
_wkeGetWidth:
	movl	_mbk_slots+456, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+456, %eax
1:
	jmp	*%eax
	.globl _wkeGetWindowHandle
_wkeGetWindowHandle:
	movl	_mbk_slots+460, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+460, %eax
1:
	jmp	*%eax
	.globl _wkeGetZoomFactor
_wkeGetZoomFactor:
	movl	_mbk_slots+464, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+464, %eax
1:
	jmp	*%eax
	.globl _wkeGlobalExec
_wkeGlobalExec:
	movl	_mbk_slots+468, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+468, %eax
1:
	jmp	*%eax
	.globl _wkeGoBack
_wkeGoBack:
	movl	_mbk_slots+472, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+472, %eax
1:
	jmp	*%eax
	.globl _wkeGoForward
_wkeGoForward:
	movl	_mbk_slots+476, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+476, %eax
1:
	jmp	*%eax
	.globl _wkeInitialize
_wkeInitialize:
	movl	_mbk_slots+480, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+480, %eax
1:
	jmp	*%eax
	.globl _wkeInitializeEx
_wkeInitializeEx:
	movl	_mbk_slots+484, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+484, %eax
1:
	jmp	*%eax
	.globl _wkeInsertCSSByFrame
_wkeInsertCSSByFrame:
	movl	_mbk_slots+488, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+488, %eax
1:
	jmp	*%eax
	.globl _wkeIsAwake
_wkeIsAwake:
	movl	_mbk_slots+492, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+492, %eax
1:
	jmp	*%eax
	.globl _wkeIsCookieEnabled
_wkeIsCookieEnabled:
	movl	_mbk_slots+496, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+496, %eax
1:
	jmp	*%eax
	.globl _wkeIsDirty
_wkeIsDirty:
	movl	_mbk_slots+500, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+500, %eax
1:
	jmp	*%eax
	.globl _wkeIsDocumentReady
_wkeIsDocumentReady:
	movl	_mbk_slots+504, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+504, %eax
1:
	jmp	*%eax
	.globl _wkeIsInitialize
_wkeIsInitialize:
	movl	_mbk_slots+508, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+508, %eax
1:
	jmp	*%eax
	.globl _wkeIsLoading
_wkeIsLoading:
	movl	_mbk_slots+512, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+512, %eax
1:
	jmp	*%eax
	.globl _wkeIsLoadingCompleted
_wkeIsLoadingCompleted:
	movl	_mbk_slots+516, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+516, %eax
1:
	jmp	*%eax
	.globl _wkeIsLoadingFailed
_wkeIsLoadingFailed:
	movl	_mbk_slots+520, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+520, %eax
1:
	jmp	*%eax
	.globl _wkeIsLoadingSucceeded
_wkeIsLoadingSucceeded:
	movl	_mbk_slots+524, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+524, %eax
1:
	jmp	*%eax
	.globl _wkeIsMainFrame
_wkeIsMainFrame:
	movl	_mbk_slots+528, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+528, %eax
1:
	jmp	*%eax
	.globl _wkeIsProcessingUserGesture
_wkeIsProcessingUserGesture:
	movl	_mbk_slots+532, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+532, %eax
1:
	jmp	*%eax
	.globl _wkeIsWebRemoteFrame
_wkeIsWebRemoteFrame:
	movl	_mbk_slots+536, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+536, %eax
1:
	jmp	*%eax
	.globl _wkeJsBindFunction
_wkeJsBindFunction:
	movl	_mbk_slots+540, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+540, %eax
1:
	jmp	*%eax
	.globl _wkeJsBindGetter
_wkeJsBindGetter:
	movl	_mbk_slots+544, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+544, %eax
1:
	jmp	*%eax
	.globl _wkeJsBindSetter
_wkeJsBindSetter:
	movl	_mbk_slots+548, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+548, %eax
1:
	jmp	*%eax
	.globl _wkeKillFocus
_wkeKillFocus:
	movl	_mbk_slots+552, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+552, %eax
1:
	jmp	*%eax
	.globl _wkeLayoutIfNeeded
_wkeLayoutIfNeeded:
	movl	_mbk_slots+556, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+556, %eax
1:
	jmp	*%eax
	.globl _wkeLoadFileW
_wkeLoadFileW:
	movl	_mbk_slots+560, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+560, %eax
1:
	jmp	*%eax
	.globl _wkeLoadHTML
_wkeLoadHTML:
	movl	_mbk_slots+564, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+564, %eax
1:
	jmp	*%eax
	.globl _wkeLoadHTMLW
_wkeLoadHTMLW:
	movl	_mbk_slots+568, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+568, %eax
1:
	jmp	*%eax
	.globl _wkeLoadHtmlWithBaseUrl
_wkeLoadHtmlWithBaseUrl:
	movl	_mbk_slots+572, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+572, %eax
1:
	jmp	*%eax
	.globl _wkeLoadURL
_wkeLoadURL:
	movl	_mbk_slots+576, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+576, %eax
1:
	jmp	*%eax
	.globl _wkeLoadURLW
_wkeLoadURLW:
	movl	_mbk_slots+580, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+580, %eax
1:
	jmp	*%eax
	.globl _wkeLoadW
_wkeLoadW:
	movl	_mbk_slots+584, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+584, %eax
1:
	jmp	*%eax
	.globl _wkeMoveToCenter
_wkeMoveToCenter:
	movl	_mbk_slots+588, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+588, %eax
1:
	jmp	*%eax
	.globl _wkeMoveWindow
_wkeMoveWindow:
	movl	_mbk_slots+592, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+592, %eax
1:
	jmp	*%eax
	.globl _wkeNetAddHTTPHeaderFieldToUrlRequest
_wkeNetAddHTTPHeaderFieldToUrlRequest:
	movl	_mbk_slots+596, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+596, %eax
1:
	jmp	*%eax
	.globl _wkeNetCancelRequest
_wkeNetCancelRequest:
	movl	_mbk_slots+600, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+600, %eax
1:
	jmp	*%eax
	.globl _wkeNetCancelWebUrlRequest
_wkeNetCancelWebUrlRequest:
	movl	_mbk_slots+604, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+604, %eax
1:
	jmp	*%eax
	.globl _wkeNetChangeRequestUrl
_wkeNetChangeRequestUrl:
	movl	_mbk_slots+608, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+608, %eax
1:
	jmp	*%eax
	.globl _wkeNetContinueJob
_wkeNetContinueJob:
	movl	_mbk_slots+612, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+612, %eax
1:
	jmp	*%eax
	.globl _wkeNetCreateWebUrlRequest
_wkeNetCreateWebUrlRequest:
	movl	_mbk_slots+616, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+616, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetExpectedContentLength
_wkeNetGetExpectedContentLength:
	movl	_mbk_slots+620, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+620, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetFavicon
_wkeNetGetFavicon:
	movl	_mbk_slots+624, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+624, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetHTTPHeaderField
_wkeNetGetHTTPHeaderField:
	movl	_mbk_slots+628, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+628, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetHttpStatusCode
_wkeNetGetHttpStatusCode:
	movl	_mbk_slots+632, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+632, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetMIMEType
_wkeNetGetMIMEType:
	movl	_mbk_slots+636, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+636, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetPostBody
_wkeNetGetPostBody:
	movl	_mbk_slots+640, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+640, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetRequestMethod
_wkeNetGetRequestMethod:
	movl	_mbk_slots+644, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+644, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetResponseUrl
_wkeNetGetResponseUrl:
	movl	_mbk_slots+648, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+648, %eax
1:
	jmp	*%eax
	.globl _wkeNetGetUrlByJob
_wkeNetGetUrlByJob:
	movl	_mbk_slots+652, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+652, %eax
1:
	jmp	*%eax
	.globl _wkeNetHoldJobToAsynCommit
_wkeNetHoldJobToAsynCommit:
	movl	_mbk_slots+656, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+656, %eax
1:
	jmp	*%eax
	.globl _wkeNetHookRequest
_wkeNetHookRequest:
	movl	_mbk_slots+660, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+660, %eax
1:
	jmp	*%eax
	.globl _wkeNetOnResponse
_wkeNetOnResponse:
	movl	_mbk_slots+664, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+664, %eax
1:
	jmp	*%eax
	.globl _wkeNetSetData
_wkeNetSetData:
	movl	_mbk_slots+668, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+668, %eax
1:
	jmp	*%eax
	.globl _wkeNetSetHTTPHeaderField
_wkeNetSetHTTPHeaderField:
	movl	_mbk_slots+672, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+672, %eax
1:
	jmp	*%eax
	.globl _wkeNetSetMIMEType
_wkeNetSetMIMEType:
	movl	_mbk_slots+676, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+676, %eax
1:
	jmp	*%eax
	.globl _wkeNetSetURL
_wkeNetSetURL:
	movl	_mbk_slots+680, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+680, %eax
1:
	jmp	*%eax
	.globl _wkeNetStartUrlRequest
_wkeNetStartUrlRequest:
	movl	_mbk_slots+684, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+684, %eax
1:
	jmp	*%eax
	.globl _wkePaint
_wkePaint:
	movl	_mbk_slots+776, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+776, %eax
1:
	jmp	*%eax
	.globl _wkePaint2
_wkePaint2:
	movl	_mbk_slots+780, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+780, %eax
1:
	jmp	*%eax
	.globl _wkePerformCookieCommand
_wkePerformCookieCommand:
	movl	_mbk_slots+784, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+784, %eax
1:
	jmp	*%eax
	.globl _wkePostURL
_wkePostURL:
	movl	_mbk_slots+788, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+788, %eax
1:
	jmp	*%eax
	.globl _wkePostURLW
_wkePostURLW:
	movl	_mbk_slots+792, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+792, %eax
1:
	jmp	*%eax
	.globl _wkeReload
_wkeReload:
	movl	_mbk_slots+796, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+796, %eax
1:
	jmp	*%eax
	.globl _wkeRepaintIfNeeded
_wkeRepaintIfNeeded:
	movl	_mbk_slots+800, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+800, %eax
1:
	jmp	*%eax
	.globl _wkeResize
_wkeResize:
	movl	_mbk_slots+804, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+804, %eax
1:
	jmp	*%eax
	.globl _wkeResizeWindow
_wkeResizeWindow:
	movl	_mbk_slots+808, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+808, %eax
1:
	jmp	*%eax
	.globl _wkeRunJS
_wkeRunJS:
	movl	_mbk_slots+812, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+812, %eax
1:
	jmp	*%eax
	.globl _wkeRunJSW
_wkeRunJSW:
	movl	_mbk_slots+816, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+816, %eax
1:
	jmp	*%eax
	.globl _wkeRunJsByFrame
_wkeRunJsByFrame:
	movl	_mbk_slots+820, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+820, %eax
1:
	jmp	*%eax
	.globl _wkeSetCookie
_wkeSetCookie:
	movl	_mbk_slots+824, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+824, %eax
1:
	jmp	*%eax
	.globl _wkeSetCookieEnabled
_wkeSetCookieEnabled:
	movl	_mbk_slots+828, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+828, %eax
1:
	jmp	*%eax
	.globl _wkeSetCookieJarFullPath
_wkeSetCookieJarFullPath:
	movl	_mbk_slots+832, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+832, %eax
1:
	jmp	*%eax
	.globl _wkeSetCookieJarPath
_wkeSetCookieJarPath:
	movl	_mbk_slots+836, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+836, %eax
1:
	jmp	*%eax
	.globl _wkeSetCspCheckEnable
_wkeSetCspCheckEnable:
	movl	_mbk_slots+840, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+840, %eax
1:
	jmp	*%eax
	.globl _wkeSetDebugConfig
_wkeSetDebugConfig:
	movl	_mbk_slots+844, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+844, %eax
1:
	jmp	*%eax
	.globl _wkeSetDeviceParameter
_wkeSetDeviceParameter:
	movl	_mbk_slots+848, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+848, %eax
1:
	jmp	*%eax
	.globl _wkeSetDirty
_wkeSetDirty:
	movl	_mbk_slots+852, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+852, %eax
1:
	jmp	*%eax
	.globl _wkeSetDragDropEnable
_wkeSetDragDropEnable:
	movl	_mbk_slots+856, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+856, %eax
1:
	jmp	*%eax
	.globl _wkeSetDragEnable
_wkeSetDragEnable:
	movl	_mbk_slots+860, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+860, %eax
1:
	jmp	*%eax
	.globl _wkeSetDragFiles
_wkeSetDragFiles:
	movl	_mbk_slots+864, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+864, %eax
1:
	jmp	*%eax
	.globl _wkeSetEditable
_wkeSetEditable:
	movl	_mbk_slots+868, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+868, %eax
1:
	jmp	*%eax
	.globl _wkeSetFileSystem
_wkeSetFileSystem:
	movl	_mbk_slots+872, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+872, %eax
1:
	jmp	*%eax
	.globl _wkeSetFocus
_wkeSetFocus:
	movl	_mbk_slots+876, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+876, %eax
1:
	jmp	*%eax
	.globl _wkeSetHandle
_wkeSetHandle:
	movl	_mbk_slots+880, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+880, %eax
1:
	jmp	*%eax
	.globl _wkeSetHandleOffset
_wkeSetHandleOffset:
	movl	_mbk_slots+884, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+884, %eax
1:
	jmp	*%eax
	.globl _wkeSetHeadlessEnabled
_wkeSetHeadlessEnabled:
	movl	_mbk_slots+888, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+888, %eax
1:
	jmp	*%eax
	.globl _wkeSetLocalStorageFullPath
_wkeSetLocalStorageFullPath:
	movl	_mbk_slots+892, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+892, %eax
1:
	jmp	*%eax
	.globl _wkeSetMediaVolume
_wkeSetMediaVolume:
	movl	_mbk_slots+896, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+896, %eax
1:
	jmp	*%eax
	.globl _wkeSetMemoryCacheEnable
_wkeSetMemoryCacheEnable:
	movl	_mbk_slots+900, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+900, %eax
1:
	jmp	*%eax
	.globl _wkeSetMouseEnabled
_wkeSetMouseEnabled:
	movl	_mbk_slots+904, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+904, %eax
1:
	jmp	*%eax
	.globl _wkeSetName
_wkeSetName:
	movl	_mbk_slots+908, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+908, %eax
1:
	jmp	*%eax
	.globl _wkeSetNavigationToNewWindowEnable
_wkeSetNavigationToNewWindowEnable:
	movl	_mbk_slots+912, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+912, %eax
1:
	jmp	*%eax
	.globl _wkeSetNpapiPluginsEnabled
_wkeSetNpapiPluginsEnabled:
	movl	_mbk_slots+916, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+916, %eax
1:
	jmp	*%eax
	.globl _wkeSetProxy
_wkeSetProxy:
	movl	_mbk_slots+920, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+920, %eax
1:
	jmp	*%eax
	.globl _wkeSetResourceGc
_wkeSetResourceGc:
	movl	_mbk_slots+924, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+924, %eax
1:
	jmp	*%eax
	.globl _wkeSetStringW
_wkeSetStringW:
	movl	_mbk_slots+928, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+928, %eax
1:
	jmp	*%eax
	.globl _wkeSetTouchEnabled
_wkeSetTouchEnabled:
	movl	_mbk_slots+932, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+932, %eax
1:
	jmp	*%eax
	.globl _wkeSetTransparent
_wkeSetTransparent:
	movl	_mbk_slots+936, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+936, %eax
1:
	jmp	*%eax
	.globl _wkeSetUserAgent
_wkeSetUserAgent:
	movl	_mbk_slots+940, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+940, %eax
1:
	jmp	*%eax
	.globl _wkeSetUserAgentW
_wkeSetUserAgentW:
	movl	_mbk_slots+944, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+944, %eax
1:
	jmp	*%eax
	.globl _wkeSetUserKeyValue
_wkeSetUserKeyValue:
	movl	_mbk_slots+948, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+948, %eax
1:
	jmp	*%eax
	.globl _wkeSetViewNetInterface
_wkeSetViewNetInterface:
	movl	_mbk_slots+952, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+952, %eax
1:
	jmp	*%eax
	.globl _wkeSetViewProxy
_wkeSetViewProxy:
	movl	_mbk_slots+956, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+956, %eax
1:
	jmp	*%eax
	.globl _wkeSetWindowTitleW
_wkeSetWindowTitleW:
	movl	_mbk_slots+960, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+960, %eax
1:
	jmp	*%eax
	.globl _wkeSetZoomFactor
_wkeSetZoomFactor:
	movl	_mbk_slots+964, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+964, %eax
1:
	jmp	*%eax
	.globl _wkeShowDevtools
_wkeShowDevtools:
	movl	_mbk_slots+968, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+968, %eax
1:
	jmp	*%eax
	.globl _wkeShowWindow
_wkeShowWindow:
	movl	_mbk_slots+972, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+972, %eax
1:
	jmp	*%eax
	.globl _wkeSleep
_wkeSleep:
	movl	_mbk_slots+976, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+976, %eax
1:
	jmp	*%eax
	.globl _wkeStopLoading
_wkeStopLoading:
	movl	_mbk_slots+980, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+980, %eax
1:
	jmp	*%eax
	.globl _wkeUpdate
_wkeUpdate:
	movl	_mbk_slots+984, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+984, %eax
1:
	jmp	*%eax
	.globl _wkeUtilSerializeToMHTML
_wkeUtilSerializeToMHTML:
	movl	_mbk_slots+988, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+988, %eax
1:
	jmp	*%eax
	.globl _wkeVisitAllCookie
_wkeVisitAllCookie:
	movl	_mbk_slots+992, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+992, %eax
1:
	jmp	*%eax
	.globl _wkeWake
_wkeWake:
	movl	_mbk_slots+996, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+996, %eax
1:
	jmp	*%eax
	.globl _wkeWebFrameGetMainFrame
_wkeWebFrameGetMainFrame:
	movl	_mbk_slots+1000, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+1000, %eax
1:
	jmp	*%eax
	.globl _wkeWebFrameGetMainWorldScriptContext
_wkeWebFrameGetMainWorldScriptContext:
	movl	_mbk_slots+1004, %eax
	testl	%eax, %eax
	jne	1f
	call	_mbk_ensure
	movl	_mbk_slots+1004, %eax
1:
	jmp	*%eax
