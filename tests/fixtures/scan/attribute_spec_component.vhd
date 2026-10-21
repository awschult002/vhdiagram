-- expect: 4 architecture rtl
-- expect: 12 entity after_attr_component
-- Attribute specification with entity class "component".
architecture rtl of top is
  component c is
  end component;
  attribute a : string;
  attribute a of c : component is "x";
begin
end architecture rtl;

entity after_attr_component is
end entity;
