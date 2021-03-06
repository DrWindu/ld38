/*
 *  Copyright (C) 2015, 2016 Simon Boyé
 *
 *  This file is part of lair.
 *
 *  lair is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  lair is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with lair.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <cstdio>

#ifdef __APPLE__
#include "fmemopen.h"
#else
#include <cstdio>
#endif

#include <lair/core/ldl.h>

#include "dialog.h"
#include "gui.h"

#include "logic_language.tab.hpp"

extern FILE* yyin;
extern DLogic* parseval;
extern bool pick;

using namespace lair;

static LdlParser* dialogParser = nullptr;
static const String* logicCode = nullptr;
void dialogParseError(const char* error) {
	dialogParser->error("Syntax error in condition: \"", *logicCode, "\"");
}

extern "C" {
	int yywrap() {
		return 1;
	}
}

void parseLogic (const String& mess, DNode& n)
{
	logicCode = &mess;
#if defined(_WIN32) && !defined(_MSC_VER)
	//yyin = tmpfile();
	yyin = fopen("tmp", "w");
	
	fputs(mess.c_str(), yyin);
	
	yyin = freopen("tmp", "r", yyin);
	
	yyparse();
	fclose(yyin);
#else
	yyin = fmemopen((char*) mess.c_str(), mess.size(), "r");
	yyparse();
#endif
	logicCode = nullptr;
	n.choice = pick;
	n.cond = parseval;
}

void readDLine (LdlParser& p, std::vector<DLine>& lines)
{
	String dood, text;
	bool ok = true;
	ok = ok && ldlRead(p, dood);
	ok = ok && ldlRead(p, text);
	if(ok) lines.push_back({dood, text});
}

void listDLines (LdlParser& p, DNode& n)
{
	if(p.valueType() != LdlParser::TYPE_LIST) {
		p.error("Expected VarList, got ", p.valueTypeName());
		p.skip();
		return;
	}

	p.enter();
	if (p.valueType() == LdlParser::TYPE_STRING)
		readDLine(p, n.lines);
	else if(p.valueType() == LdlParser::TYPE_LIST)
		while (p.valueType() != LdlParser::TYPE_END)
		{
			p.enter();
			readDLine(p, n.lines);
			p.leave();
		}
	else {
		p.error("Expected VarList or String, got ", p.valueTypeName());
		p.skip();
	}
	p.leave();
}

DEffect parseDEffect (String s)
{
	int p = 0;
	DEffect e;
	switch (s[0])
	{
		case '+':
			e.t = V_INCR;
			break;
		case '-':
			e.t = V_DECR;
			break;
		case '=':
			e.t = V_SET;
			break;

		case '!':
			e.t = UNFLAG;
			break;
		default:
			e.t = FLAG;
			p--;
	}

	s = s.substr(++p);
	p = s.find('[');
	
	e.name = s.substr(0,p);
	if (p != std::string::npos)
		e.val = atoi(s.substr(p+1, s.size()-p-2).data()); // Ugh.

	return e;
}

void listDEffects (LdlParser& p, DNode& n)
{
	if (p.valueType() == LdlParser::TYPE_STRING)
	{
		n.effects.push_back(parseDEffect(p.getString()));
		p.next();
		return;
	}

	p.enter();
	while (p.valueType() != LdlParser::TYPE_END)
	{
		n.effects.push_back(parseDEffect(p.getString()));
		p.next();
	}
	p.leave();
}

void stackNexts (LdlParser& p, std::vector<String>& next)
{
	if (p.valueType() == LdlParser::TYPE_STRING)
	{
		next.push_back(p.getString());
		p.next();
		return;
	}

	p.enter();
	while (p.valueType() != LdlParser::TYPE_END)
	{
		next.push_back(p.getString());
		p.next();
	}
	p.leave();
}

Dialog::Dialog(MainState* ms, const Path& filename)
{
	Path::IStream in(filename.native().c_str());
	lairAssert(in.good());
	ErrorList el;
	LdlParser p(&in, filename.utf8String(), &el, LdlParser::CTX_MAP);
	dialogParser = &p;

	std::vector<DNode*> nodes;
	std::vector<std::vector<String>> nexts;

	if(p.valueType() != LdlParser::TYPE_MAP) {
		p.error("Expected VarMap, got ", p.valueTypeName());
		p.skip();
		return;
	}

	p.enter();
	while (p.valueType() != LdlParser::TYPE_END)
	{
		if(p.valueType() != LdlParser::TYPE_MAP) {
			p.error("Expected VarMap, got ", p.valueTypeName());
			p.skip();
			continue;
		}
		
		DNode* n = new DNode();
		n->cond = NULL;
		nodes.push_back(n);
		nexts.push_back(std::vector<String>());

		n->id = p.getKey();
		p.enter();
		while (p.valueType() != LdlParser::TYPE_END)
		{
			String nkey = p.getKey();
			if (nkey == "conditions") {
				if(p.valueType() != LdlParser::TYPE_STRING) {
					p.error("Expected String, got ", p.valueTypeName());
					p.skip();
				}
				else {
					String cond;
					ldlRead(p, cond);
					parseLogic(cond, *n);
				}
			}
			else if (nkey == "lines")
				listDLines(p, *n);
			else if (nkey == "effects")
				listDEffects(p, *n);
			else if (nkey == "next")
				stackNexts(p, nexts.back());
			else
				p.skip();
		}
		p.leave();		
	}
	p.leave();

	el.log(dbgLogger);

	assert( nodes.size() == nexts.size() );
	for (int i = 0 ; i < nexts.size() ; i++)
		for (int j = 0 ; j < nexts[i].size() ; j++)
			for (int k = 0 ; k < nodes.size() ; k++)
				if (nodes[k]->id == nexts[i][j])
					nodes[i]->next.push_back(nodes[k]);

	_ms = ms;
	_wombatFallback = new DNode();
	_wombatFallback->lines.push_back({"NPC", "I have nothing to say to you because you are not a wombat."});
	_start = nodes[0];
	_line = 0;
	_choice = -1;
}

Dialog::~Dialog () { }

void Dialog::say (DLine l)
{
	if(l.chara == "exec") {
		_ms->exec(l.text);
	}
	else {
		_ms->_gui.setText(l.text);
		_ms->_gui.setCharacterSprite("face_" + l.chara + ".png");
	}
}

void Dialog::beginDialog ()
{
	_current = _start;
	_ms->_gui.showDialog();
	_ms->_gui.showCharacter();
	if(_current->lines[0].chara.empty())
		stepDialog();
	else
		say(_current->lines[0]);
}

bool Dialog::stepDialog ()
{
	// Step forward: in a choice prompt ?
	if (_choice == -1)
	{
		//No: Iterate on current lines...
		_line++;

		//...or go to next step if needed.
		if (_line >= _current->lines.size())
		{
			_line = 0;

			// Apply effects.
			apply(_current->effects);

			// No next dialog: over and out.
			if (_current->next.size() == 0)
			{
				_ms->_gui.hideDialog();
				_ms->_gui.hideCharacter();
				_current = NULL;
				_choice = -1;
				return true;
			}

			// Pick first allowed next dialog... if any.
			bool panic = true;
			for (int i = 0 ; i < _current->next.size() ; i++)
				if (check(_current->next[i]->cond))
				{
					panic = false;
					_current = _current->next[i];
					break;
				}

			if (panic) // Impossibruh !
				_current = _wombatFallback;
		}
	}
	else
	{
		// Yes: Pick currently selected choice.
		_current = _choices[_choice].second;
		_choices.clear();
		_choice = -1;
	}

	// Display new step : reached a choice prompt ?
	if (_current->next.size() && _current->next[0]->choice)
	{
		// Yes: Build and display list of available choices (the rest are hidden).
		for (int i = 0 ; i < _current->next.size() ; i++)
			if (check(_current->next[i]->cond))
				_choices.push_back(std::pair<String, DNode*>(_current->next[i]->lines[0].text, _current->next[i]));
		_choice = 0;
		offerChoice();
	}
	else // Display current line.
		say(_current->lines[_line]);
	return false;
}

void Dialog::offerChoice ()
{
	assert (_choice != -1);
	String out(_current->lines[0].text + "\n");
	for (int i = 0 ; i < _choices.size() ; i++)
	{
		if (i == _choice)
			out += "> ";
		else
			out += "  ";
		out += _choices[i].first;
		out += "\n";
	}
	_ms->_gui.setText(out);
	_ms->_gui.setCharacterSprite("face_" + _current->lines[0].chara + ".png");
}

void Dialog::selectUp ()
{
	if (_choice == -1) return;
	_choice = (_choice-1+_choices.size()) % _choices.size();
	offerChoice();
}

void Dialog::selectDown ()
{
	if (_choice == -1) return;
	_choice = (_choice+1) % _choices.size();
	offerChoice();
}

bool Dialog::check (const DLogic* cond)
{
	if (!cond) return true;
	switch (cond->t)
	{
		case L_NOT:
			_ms->log().info("Test NOT");
			return !check(cond->left);
		case L_AND:
			_ms->log().info("Test AND");
			return check(cond->left) && check(cond->right);
		case L_OR:
			_ms->log().info("Test OR");
			return check(cond->left) || check(cond->right);
		case C_GREATER:
			_ms->log().info("Test ", cond->name, " (", _ms->getData(cond->name), ") > ", cond->val);
			return _ms->getData(cond->name) > cond->val;
		case C_LESSER:
		_ms->log().info("Test ", cond->name, " (", _ms->getData(cond->name), ") < ", cond->val);
			return _ms->getData(cond->name) < cond->val;
		case C_EQUAL:
			_ms->log().info("Test ", cond->name, " (", _ms->getData(cond->name), ") = ", cond->val);
			return _ms->getData(cond->name) == cond->val;
		case YFLAG:
			_ms->log().info("Test ", cond->name, " (", _ms->getData(cond->name), ") = true");
			return _ms->getData(cond->name);
		case NFLAG:
			_ms->log().info("Test ", cond->name, " (", _ms->getData(cond->name), ") = false");
			return !_ms->getData(cond->name);
		default:
			assert (false);
	}
}

void Dialog::apply (const std::vector<DEffect>& effects)
{
	for (int i = 0 ; i < effects.size() ; i++)
	{
		DEffect e = effects[i];
		switch (e.t)
		{
			case V_INCR:
				_ms->getData(e.name) += e.val;
				break;
			case V_DECR:
				_ms->getData(e.name) -= e.val;
				break;
			case V_SET:
				_ms->getData(e.name) = e.val;
				break;
			case FLAG:
				_ms->getData(e.name) = 1;
				break;
			case UNFLAG:
				_ms->getData(e.name) = 0;
				break;
		}
		_ms->log().info("Set ", e.name, " = ", _ms->getData(e.name));
	}
}
