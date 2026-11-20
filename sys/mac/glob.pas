unit glob;

{$mode objfpc}{$H+}

interface

function glob_match(const Pattern, Text: AnsiString): Boolean;

implementation

uses
  StrUtils;

function glob_match(const Pattern, Text: AnsiString): Boolean;
begin
  Result := IsWild(Text, Pattern, False);
end;

end.
