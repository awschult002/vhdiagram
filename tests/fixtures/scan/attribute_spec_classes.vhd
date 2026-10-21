-- expect: 5 entity e
-- expect: 11 architecture rtl
-- expect: 34 entity after_attr_classes
-- Attribute specifications for many entity classes in one file.
entity e is
  port (s : in bit);
  attribute a : string;
  attribute a of e : entity is "x";
end entity e;

architecture rtl of e is
  type t is range 0 to 10
    units
      u;
    end units;
  signal sig : bit;
  function f return bit is begin return '0'; end function;
  procedure q is begin end procedure;
  component c is end component;
  package inner is end package inner;
  attribute a of rtl : architecture is "x";
  attribute a of sig : signal is "x";
  attribute a of lbl : label is "x";
  attribute a of t : type is "x";
  attribute a of f : function is "x";
  attribute a of q : procedure is "x";
  attribute a of t : units is "x";
  attribute a of c : component is "x";
  attribute a of inner : package is "x";
begin
  lbl: s <= '1';
end architecture rtl;

entity after_attr_classes is
end entity;
