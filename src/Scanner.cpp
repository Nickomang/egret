/*  Scanner.cpp: scanner for regular expression, used by parser 

    Copyright (C) 2016  Eric Larson and Anna Kirk
    elarson@seattleu.edu

    Some code in this file was derived from a RE->NFA converter
    developed by Eli Bendersky.

    This file is part of EGRET.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Scanner.h"
#include "Stats.h"
#include "error.h"

using namespace std;

void
Scanner::init(string in)
{
  unsigned int idx = 0;
  bool in_set = false;	// set to true when in the middle of set [] 
  while (idx < in.length()) {

    Token token;
    switch (in[idx]) {

    case '\\':
    {
      // Look at character after backslash
      char c = get_next_char(in, idx);
      switch (c) {
	// Look for character classes
	case 'd':
	case 'D':
	case 'w':
	case 'W':
	case 's':
	case 'S':
	  token.type = CHAR_CLASS;
	  token.character = c;
	  break;
	// Treat \A the same as ^ (only differ in multi-line which is not supported)
	case 'A':
	  token.type = CARET;
	  break;
	// Treat \Z the same as $ (only differ in multi-line which is not supported)
	case 'Z':
	  token.type = DOLLAR;
	  break;
	// \b is backspace in a character set (unsupported) and word boundary otherwise
        case 'b':
	  if (in_set) {
	    throw EgretException("ERROR: contains unsupported character \\b");
	  }
	  else {
	    token.type = WORD_BOUNDARY;
	    addWarning("Regex contains ignored \\b");
	  }
	  break;
	// \B is also treated as word boundary
	case 'B':
	  token.type = WORD_BOUNDARY;
	  addWarning("Regex contains ignored \\B");
	  break;
	// Escaped characters are unsupported
        case 'a':
        case 'f':
	case 'n':
	case 'r':
	case 't':
	case 'v':
	case 'p':
	{
	  stringstream s;
	  s << "ERROR: contains unsupported character \\" << c;
	  throw EgretException(s.str());
	}
	case '\\':
	  token.type = CHARACTER;
	  token.character = '\\';
	  break;
        case '\'':
	  token.type = CHARACTER;
	  token.character = '\'';
	  break;
        case '\"':
	  token.type = CHARACTER;
	  token.character = '\"';
	  break;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	  token = process_octal(in, idx, c);
	  break;
	case 'x':
	  token = process_hex(in, idx, 2);
	  break;
	case 'u':
	  token = process_hex(in, idx, 4);
	  break;
	case 'U':
	  token = process_hex(in, idx, 8);
	  break;
	// Everything else is a character - used for \(, \$, etc. 
	default:
	  token.type = CHARACTER;
	  token.character = c;
      }
      break;
    }

    case '[':
      // check if already in set --> matches left bracket character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> start of a set
      else {
        token.type = LEFT_BRACKET;
	in_set = true;
      }
      break;

    case ']': 
      // check if first in set --> matches right bracket character
      if (in_set && tokens.size() > 0 && tokens.back().type == LEFT_BRACKET) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check if in set but not first --> ends the set
      else if (in_set) {
	token.type = RIGHT_BRACKET;
	in_set = false;
      }
      // otherwise --> matches right bracket character
      else {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      break;

    case '-':
      // check if first in set --> matches hyphen character
      if (in_set && tokens.size() > 0 && tokens.back().type == LEFT_BRACKET) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check if last in set --> matches hyphen chatacter
      else if (in_set && (idx + 1) < in.length() && in[idx + 1] == ']') {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check if in set but not first or last --> indicates a range hyphen
      else if (in_set) {
	token.type = HYPHEN;
      }
      // otherwise --> matches hyphen character
      else {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      break;

    case '|':
      // check if in set --> matches vertical bar character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> alternation
      else {
        token.type = ALTERNATION;
      }
      break;

    case '*':
      // check if in set --> matches asterisk character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check for lazy '*?' --> Kleene star
      // (no distinction is made for lazy version)
      else if (!in_set && (idx + 1) < in.length() && in[idx + 1] == '?') {
	idx++; // skip over the '?'
	token.type = STAR;
      }
      // otherwise --> Kleene star
      else {
        token.type = STAR;
      }
      break;

    case '+':
      // check if in set --> matches plus character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check for lazy '+?' --> plus
      // (no distinction is made for lazy version)
      else if (!in_set && (idx + 1) < in.length() && in[idx + 1] == '?') {
	idx++; // skip over the '?'
	token.type = PLUS;
      }
      // otherwise --> plus (1 or more repetition)
      else {
        token.type = PLUS;
      }
      break;

    case '?':
      // check if in indicates extension
      if (tokens.size() > 0 && tokens.back().type == LEFT_PAREN) {
	token = process_extension(in, idx);
      }
      // check if in set --> matches question mark character
      else if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // check for lazy '??' --> optional operator
      // (no distinction is made for lazy version)
      else if (!in_set && (idx + 1) < in.length() && in[idx + 1] == '?') {
	idx++; // skip over the second '?'
	token.type = QUESTION;
      }
      // otherwise --> optional operator (matches 0 or 1)
      else {
        token.type = QUESTION;
      }
      break;

    case '(':
      // check if in set --> matches left parentheses character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> start of a group
      else {
        token.type = LEFT_PAREN;
      }
      break;

    case ')':
      // check if in set --> matches right parentheses character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> end of a group
      else {
        token.type = RIGHT_PAREN;
      }
      break;

    case '.':
      // check if in set --> matches period character
      if (in_set) {
        token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> character class that includes everything
      else {
        token.type = CHAR_CLASS;
	token.character = in[idx];
      }
      break;

    case '{':
      // check if in set --> mataches left brace
      if (in_set) {
	token.type = CHARACTER;
        token.character = in[idx];
      }
      // otherwise --> process a possible repeat clause
      else {
        token = process_repeat(in, idx);
        // check for lazy repeat - skip over the '?' if present
        if (token.type != CHARACTER && (idx + 1) < in.length() && in[idx + 1] == '?') {
	  idx++;
        }
      }
      break;

    case '^':
      token.type = CARET;
      break;

    case '$':
      token.type = DOLLAR;
      break;

    default:
      token.type = CHARACTER;
      token.character = in[idx];
    }

    tokens.push_back(token);
    idx++;
  }
  
  index = 0;
}

char
Scanner::get_next_char(string in, unsigned int &idx)
{
  idx++;
  if (idx >= in.length()) {
    throw EgretException("ERROR: Input string ended prematurely");
  }
  return in[idx];
}

Token
Scanner::process_octal(string in, unsigned int &idx, char first_digit)
{
  bool only_one_digit = false;
  bool has_three_digits = false;
  char second_digit;
  char third_digit;

  // grab the second digit if one exists
  if (idx + 1 >= in.length()) {
    only_one_digit = true;
  } else {
    second_digit = in[idx + 1];
    if (second_digit < '0' || second_digit > '7') {
      only_one_digit = true;
    }
  }

  // only one digit - either null or backreference (both are unsupported)
  if (only_one_digit) {
    if (first_digit == '0') {
      throw EgretException("ERROR: contains unsupported character \\0");
    } else {
      stringstream s;
      s << "ERROR: contains unsupported backreference value \\" << first_digit;
      throw EgretException(s.str());
    }
  }

  // grab the third digit if one exists
  if (idx + 2 < in.length()) {
    third_digit = in[idx + 2];
    if (third_digit >= '0' && third_digit <= '7') {
      has_three_digits = true;
    }
  }

  // determine the octal value
  int octal_value;
  if (has_three_digits) {
    octal_value = ((first_digit - '0') * 64) + ((second_digit - '0') * 8) +
      (third_digit -'0');
    idx += 2;
  } else {
    octal_value = ((first_digit - '0') * 8) + (second_digit - '0');
    idx++;
  }

  // check the validity of the octal value
  if (octal_value < 32 || octal_value > 126) {
    stringstream s;
    s << "ERROR: contains unsupported octal value " << octal_value;
    throw EgretException(s.str());
  }

  // return the octal value
  Token token;
  token.type = CHARACTER;
  token.character = octal_value;

  return token;
}
    
Token
Scanner::process_hex(string in, unsigned int &idx, int num_digits)
{
  // if number of digits is greater than 2, the extra (left) digits must be zero
  // as no unicode (outside ascii) are supported.
  for (int i = 2; i < num_digits; i++) {
    char d = get_next_char(in, idx);
    if (d != '0') {
      stringstream s;
      s << "ERROR: Unsupported " << num_digits << "-digit hex number";
      throw EgretException(s.str());
    }
  }

  // get the new two characters which must be hex digits
  char first_digit = get_next_char(in, idx);
  char second_digit = get_next_char(in, idx);

  // compute the hex value
  int hex_value;
  if (first_digit >= '0' && first_digit <= '9') {
    hex_value = (first_digit - '0') * 16;
  }
  else if (first_digit >= 'A' && first_digit <= 'F') {
    hex_value = (first_digit - 'A' + 10) * 16;
  }
  else if (first_digit >= 'a' && first_digit <= 'f') {
    hex_value = (first_digit - 'a' + 10) * 16;
  }
  else {
    stringstream s;
    s << "ERROR: Invalid hex digit " << first_digit;
    throw EgretException(s.str());
  }
  if (second_digit >= '0' && second_digit <= '9') {
    hex_value += (second_digit - '0');
  }
  else if (second_digit >= 'A' && second_digit <= 'F') {
    hex_value += (second_digit - 'A' + 10);
  }
  else if (second_digit >= 'a' && second_digit <= 'f') {
    hex_value += (second_digit - 'a' + 10);
  }
  else {
    stringstream s;
    s << "ERROR: Invalid hex digit " << second_digit;
    throw EgretException(s.str());
  }

  // check the validity of the hex value
  if (hex_value < 32 || hex_value > 126) {
    stringstream s;
    s << "ERROR: contains unsupported hex value " << hex_value;
    throw EgretException(s.str());
  }

  // return the hex value
  Token token;
  token.type = CHARACTER;
  token.character = hex_value;

  return token;
}

Token
Scanner::process_extension(string in, unsigned int &idx)
{
  Token token;

  // get type of extension
  char ext = get_next_char(in, idx);
  switch(ext) {
  
  case ':':
    token.type = NO_GROUP_EXT;
    break;

  case 'P':
  {
    char c = get_next_char(in, idx);
    if (c == '=') {
      throw EgretException("ERROR: Unsupported named backreference: (?P=");
    }
    if (c != '<') {
      throw EgretException("ERROR: Improperly specified named group - expected < after (?P");
    }
    while (c != '>') {
      c = get_next_char(in, idx);
    }
    token.type = NAMED_GROUP_EXT;
    break;
  }

  case '#':
  case '=':
  case '!':
  {
    stringstream s;
    s << "Regex contains ignored extension ?" << ext;
    addWarning(s.str());
    token.type = IGNORED_EXT;
    break;
  }

  case '<':
  {
    char c = get_next_char(in, idx);
    if (c == '=' || c == '!') {
      stringstream s;
      s << "Regex contains ignored extension ?<" << c;
      addWarning(s.str());
      token.type = IGNORED_EXT;
    }
    else {
      stringstream s;
      s << "ERROR: Unsupported extension ?<" << c;
      throw EgretException(s.str());
    }
    break;
  }

  default:
  {
    stringstream s;
    s << "ERROR: Unsupported extension ?" << ext;
    throw EgretException(s.str());
  }

  }

  return token;
}

Token
Scanner::process_repeat(string in, unsigned int &idx)
{
  // Based on execution of Python, the repeat quantifier must have one of these forms:
  // {n}  	: matches exactly n times
  // {n,}	: matches at least n times
  // {,m}	: matches no more than m times
  // {n,m}	: matches at least n times but no more than m times
  //
  // The values n and m must be integers consisting of one or more digits.  Any deviation
  // from the above, including the addition of spaces, will result in a literal match.
  // For example, the only string to match the regular expression "a{3, 4}" is "a{3, 4}".
  //
  // The only error that is possible is when the repeat quantifier is of the form {n,m} and
  // n > m.
  //
  // This program will abort on pointless quantifiers {0}, {,0}, and {0,0}.

  // Save the current index and a default return struct that matches the "{" only.  If there
  // is a formatting error, idx will be restored to the current index and the "{" character
  // token is returned.
  // 
  int current_idx = idx;
  Token token;
  token.type = CHARACTER;
  token.character = '{';

  // Keep looping while reading in digits.
  string count_str = "";
  char c = get_next_char(in, idx);
  while (isdigit(c)) {
    count_str += c;
    c = get_next_char(in, idx);
  }

  // Stopping character is ',' --> lower bound complete
  if (c == ',') {
    // No lower bound number --> use -1 to represent no lower bound
    if (count_str == "") {
      token.repeat_lower = -1;
    }
    // Otherwise store lower bound
    else {
      stringstream ss(count_str);
      ss >> token.repeat_lower; 
    }
  }

  // Stopping character is '}' --> Exact match instance of repeat quantifier
  else if (c == '}') {
    // No number --> string is {} --> literal match
    if (count_str == "") {
      idx = current_idx;
      return token;
    }
    // Otherwise return REPEAT token with identical lower and upper bounds
    stringstream ss(count_str);
    ss >> token.repeat_lower; 
    token.repeat_upper = token.repeat_lower;
    token.type = REPEAT;
    if (token.repeat_upper == 0) {
      throw EgretException("ERROR: pointless repeat quantifier {0}");
    }
    return token;
  }

  // Stopping chatacter is something else --> literal match (ill-formed quantifier)
  else {
    idx = current_idx;
    return token;
  }

  // Grab second digit after the comma if one exists
  count_str = "";
  c = get_next_char(in, idx);
  while (isdigit(c)) {
    count_str += c;
    c = get_next_char(in, idx);
  }

  // Stopping character is '}'
  if (c == '}') {
    // No upper bound number --> use -1 to represent no upper bound
    if (count_str == "") {
      token.repeat_upper = -1;
    }
    // Otherwise store upper bound
    else {
      stringstream ss(count_str);
      ss >> token.repeat_upper; 
    }

    // Check for no bounds, at least one must be present.  If not --> literal match
    if (token.repeat_lower == -1 && token.repeat_upper == -1) {
      idx = current_idx;
      return token;
    }

    // If no lower bound, adjust lower bound to 0
    if (token.repeat_lower == -1) token.repeat_lower = 0;
    
    // If no upper bound, return now.
    if (token.repeat_upper == -1) {
      token.type = REPEAT;
      return token;
    }

    // Check that lower bound is less than or equal to the upper bound
    if (token.repeat_lower > token.repeat_upper) {
      stringstream s;
      s << "ERROR: Invalid repeat quantifier: lower bound " << token.repeat_lower
	<< " is greater than upper bound " << token.repeat_upper << endl;
      throw EgretException(s.str());
    }

    // Check for the nonsensical upper bound of zero {0,0}
    if (token.repeat_upper == 0) {
      throw EgretException("ERROR: pointless repeat quantifier {0,0}");
    }

    token.type = REPEAT;
    return token;
  }

  // Stopping character is somethine else --> literal match (ill-formed quantifier)
  else {
    idx = current_idx;
    return token;
  }
}

TokenType
Scanner::get_type()
{
  if (index < tokens.size())
    return tokens[index].type;
  else
    return ERR;
}

string
Scanner::get_type_str()
{
  return token_type_to_str(get_type());
}

int
Scanner::get_repeat_lower()
{
  TokenType type = get_type();
  assert(type == REPEAT);

  return tokens[index].repeat_lower;
}

int
Scanner::get_repeat_upper()
{
  TokenType type = get_type();
  assert(type == REPEAT);

  return tokens[index].repeat_upper;
}

char
Scanner::get_character()
{
  TokenType type = get_type();
  assert(type == CHARACTER || type == CHAR_CLASS);

  return tokens[index].character;
}

void
Scanner::advance()
{
  index++;
}

bool
Scanner::is_concat()
{
  TokenType prev_type = tokens[index - 1].type; // previous character type
  TokenType next_type = get_type(); // current character type

  if (index >= tokens.size()) return false;

  bool valid_prev_type = 
    prev_type == STAR ||
    prev_type == PLUS ||
    prev_type == QUESTION ||
    prev_type == REPEAT ||
    prev_type == RIGHT_PAREN ||
    prev_type == CHARACTER ||
    prev_type == CARET ||
    prev_type == DOLLAR ||
    prev_type == WORD_BOUNDARY ||
    prev_type == CHAR_CLASS ||
    prev_type == RIGHT_BRACKET;

  bool invalid_next_type =
    next_type == ALTERNATION ||
    next_type == STAR ||
    next_type == PLUS ||
    next_type == QUESTION ||
    next_type == REPEAT ||
    next_type == RIGHT_PAREN ||
    next_type == RIGHT_BRACKET;

  return valid_prev_type && !invalid_next_type;
}

bool
Scanner::is_char_range()
{
  if (index > tokens.size() - 3) return false;
  if (tokens[index+1].type != HYPHEN) return false;

  if (tokens[index].type == CHARACTER &&
      tokens[index+2].type == CHARACTER) {
    if (tokens[index].character > tokens[index+2].character) {
      stringstream s;
      s << "ERROR: Improperly formed range "
        << tokens[index].character << "-"
	<< tokens[index+2].character << endl;
      throw EgretException(s.str());
    }
    return true;
  }
  else if (tokens[index].type == CHARACTER &&
      tokens[index+2].type == CHAR_CLASS) {
    throw EgretException("ERROR: Improperly constructed range using char class");
  }
  else if (tokens[index].type == CHAR_CLASS &&
      tokens[index+2].type == CHARACTER) {
    throw EgretException("ERROR: Improperly constructed range using char class");
  }
  else if (tokens[index].type == CHAR_CLASS &&
      tokens[index+2].type == CHAR_CLASS) {
    throw EgretException("ERROR: Improperly constructed range using char class");
  }
  return false;
}

void
Scanner::print()
{
  cout << "Scanner: " << endl;
  for (unsigned i = 0; i < tokens.size(); i++) {
    cout << token_type_to_str(tokens[i].type);
    if (tokens[i].type == REPEAT) {
      cout << ":" << tokens[i].repeat_lower << "," << tokens[i].repeat_upper;
    }
    if (tokens[i].type == CHARACTER || tokens[i].type == CHAR_CLASS) {
      cout << ":" << tokens[i].character;
    }
    cout << endl;
  }
  cout << endl;
}

string
Scanner::token_type_to_str(TokenType type)
{
  switch (type) {
  case ALTERNATION:	return "ALTERNATION";
  case STAR:		return "STAR";
  case PLUS:		return "PLUS";
  case QUESTION:	return "QUESTION";
  case REPEAT:		return "REPEAT";
  case LEFT_PAREN:	return "LEFT_PAREN";
  case RIGHT_PAREN:	return "RIGHT_PAREN";
  case CHARACTER:	return "CHARACTER";
  case CHAR_CLASS:	return "CHAR_CLASS";
  case LEFT_BRACKET:	return "LEFT_BRACKET";
  case RIGHT_BRACKET:	return "RIGHT_BRACKET";
  case CARET:		return "CARET";
  case DOLLAR:		return "DOLLAR";
  case WORD_BOUNDARY:	return "WORD_BOUNDARY";
  case HYPHEN:		return "HYPHEN";
  case NO_GROUP_EXT:	return "NO_GROUP_EXT";
  case NAMED_GROUP_EXT: return "NAMED_GROUP_EXT";
  case IGNORED_EXT: 	return "IGNORED_EXT";
  case ERR: 		return "<ERROR> (or end of regex)";
  default:  
    throw EgretException("ERROR (INTERNAL): unexpected token type");
  }
}

void
Scanner::add_stats(Stats &stats)
{
  stats.add("SCANNER", "Tokens", tokens.size());
}
