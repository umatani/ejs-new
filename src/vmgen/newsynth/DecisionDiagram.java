package vmgen.newsynth;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;

import vmgen.RuleSet;
import vmgen.RuleSet.Rule;
import vmgen.newsynth.LLRuleSet.LLRule;
import vmgen.type.VMRepType;
import vmgen.type.VMRepType.HT;
import vmgen.type.VMRepType.PT;

public class DecisionDiagram {
	public static final boolean DEBUG_COMMENT = true;
	public static final int MERGE_LEVEL = 2; // 0-2: 0 is execution spped oriendted, 2 is size oriented
	
	static final int DISPATCH_TAGPAIR = 0;
	static final int DISPATCH_PT_BASE = 10;
	static final int DISPATCH_HT_BASE = 20;
	static final int[] DISPATCH_PLAN = {
			DISPATCH_TAGPAIR,
			DISPATCH_PT_BASE + 0,
			DISPATCH_PT_BASE + 1,
			DISPATCH_HT_BASE + 0,
			DISPATCH_HT_BASE + 1
	};
	
	static abstract class Node {
		abstract Object accept(NodeVisitor visitor);		
		// returns the number of distinct destinations
		abstract int size();
		int depth() {
			int max = 0;
			for (Node child: getChildren()) {
				int d = child.depth();
				if (d > max)
					max = d;
			}
			return max + 1;
		}
		abstract ArrayList<Node> getChildren();
		
		abstract boolean isCompatibleTo(Node other);
		abstract boolean isSingleLeafTree();
		// slt should be SIngelLeafTree
		abstract boolean isAbsobable(Node slt);
		// returns a merged node
		// other should be compatible with this
		// this method does not mutate this object
		abstract Node merge(Node other);
		abstract void mergeChildren();
		abstract Node skipNoChoice();
	}
	
	static class NodeVisitor {
		Object visitLeaf(Leaf node) {
			return null;
		}
		Object visitTagNode(TagNode<?> node) {
			return null;
		}
		Object visitTagPairNode(TagPairNode node) {
			return visitTagNode(node);
		}
		Object visitPTNode(PTNode node) {
			return visitPTNode(node);
		}
		Object visitHTNode(HTNode node) {
			return visitHTNode(node);
		}
	}
	
	static class Leaf extends Node {
		LLRuleSet.LLRule rule;
		Leaf(LLRuleSet.LLRule rule) {
		 	this.rule = rule;
		}
		LLRuleSet.LLRule getRule() {
			return rule;
		}
		@Override
		Object accept(NodeVisitor visitor) {
			return visitor.visitLeaf(this);
		}
		@Override
		int size() {
			return 0;
		}
		@Override
		ArrayList<Node> getChildren() {
			return new ArrayList<Node>();
		}
		@Override
		boolean isCompatibleTo(Node otherx) {
			if (otherx instanceof Leaf) {
				Leaf other = (Leaf) otherx;
				if (rule.getHLRule() == other.getRule().getHLRule()) {
					System.out.println("isCompatible("+this+","+otherx+") = true");
					return true;
				}
			}
			System.out.println("isCompatible("+this+","+otherx+") => false");
			return false;
		}
		@Override
		boolean isSingleLeafTree() {
			return true;
		}
		@Override
		boolean isAbsobable(Node otherx) {
			return isCompatibleTo(otherx);
		}
		@Override
		Node merge(Node otherx) {
			return this;
		}
		@Override
		void mergeChildren() {}
		@Override
		Node skipNoChoice() {
			return this;
		}
	}
	static abstract class TagNode<T> extends Node {
		int opIndex;
		HashMap<T, Node> branches = new HashMap<T, Node>();
		
		TagNode(int opIndex) {
			this.opIndex = opIndex;
		}
		void addBranch(TreeDigger digger, T tag) {
			Node child = branches.get(tag);
			child = digger.dig(child);
			branches.put(tag, child);
		}
		@Override
		Object accept(NodeVisitor visitor) {
			return visitor.visitTagNode(this);
		}
		@Override
		int size() {
			return makeChildToTagsMap(branches).size();
		}
		@Override
		ArrayList<Node> getChildren() {
			LinkedHashSet<Node> s = new LinkedHashSet<Node>();
			for (T tag: branches.keySet())
				s.add(branches.get(tag));
			return new ArrayList<Node>(s);
		}
		int getOpIndex() {
			return opIndex;
		}
		boolean hasCompatibleBranches(TagNode<T> other) {
			if (opIndex != other.opIndex)
				return false;
			System.out.println("hasCompatibleBranches("+this+", "+other+")");
			LinkedHashSet<T> union = new LinkedHashSet<T>(branches.keySet());
			union.addAll(other.branches.keySet());
			for (T tag: union) {
				Node thisChild = branches.get(tag);
				Node otherChild = other.branches.get(tag);
				if (thisChild != null && otherChild != null && !thisChild.isCompatibleTo(otherChild))
					return false;
			}
			return true;
		}
		@Override
		boolean isSingleLeafTree() {
			if (size() == 1)
				return branches.values().iterator().next().isSingleLeafTree();
			return false;
		}
		@Override
		boolean isAbsobable(Node sltx) {
			if (sltx.getClass() != getClass()) {
				System.out.println("------\n");
				System.out.println(generateCodeForNode(this, new String[] {"a1", "a2"}));
				System.out.println("---\n");
				System.out.println(generateCodeForNode(sltx, new String[] {"b1", "b2"}));
				System.out.println("------\n");
				throw new Error("class mismatch");
			}
			TagNode<T> slt = (TagNode<T>) sltx;
			HashMap<Node, LinkedHashSet<T>> childToTags = makeChildToTagsMap(branches);
			for (T tag: slt.branches.keySet()) {
				Node sltChild = slt.branches.get(tag);
				if (branches.get(tag) != null) {
					Node child = branches.get(tag);
					if (!child.isAbsobable(sltChild))
						return false;
				} else {
					boolean found = false;
					for (Node child: childToTags.keySet())
						if (child.isAbsobable(sltChild)) {
							found = true;
							break;
						}
					if (!found)
						return false;
				}
			}
			return true;
		}
		void makeMergedNode(TagNode<T> n1, TagNode<T> n2) {
			LinkedHashSet<T> union = new LinkedHashSet<T>(n1.branches.keySet());
			union.addAll(n2.branches.keySet());
			for (T tag: union) {
				Node c1 = n1.branches.get(tag);
				Node c2 = n2.branches.get(tag);
				if (c1 == null)
					branches.put(tag, c2);
				else if (c2 == null)
					branches.put(tag, c1);
				else {
					Node child = c1.merge(c2);
					branches.put(tag, child);
				}
			}
			mergeChildren();
		}
		HashMap<Node, LinkedHashSet<T>> makeChildToTagsMap(HashMap<T, Node> tagToChild) {
			HashMap<Node, LinkedHashSet<T>> childToTags = new HashMap<Node, LinkedHashSet<T>>();
			for (T tag: tagToChild.keySet()) {
				Node child = tagToChild.get(tag);
				LinkedHashSet<T> tags = childToTags.get(child);
				if (tags == null) {
					tags = new LinkedHashSet<T>();
					childToTags.put(child, tags);
				}
				tags.add(tag);
			}
			return childToTags;
		}
		HashMap<Node, LinkedHashSet<T>> getChildToTagsMap() {
			return makeChildToTagsMap(branches);
		}
		@Override
		void mergeChildren() {
			HashMap<Node, LinkedHashSet<T>> childToTags = makeChildToTagsMap(branches);	
			Node[] children = new Node[childToTags.size()];
			boolean[] hasMerged = new boolean[children.length];
			{
				int i = 0;
				for (Node child: childToTags.keySet()) {
					child.mergeChildren();
					children[i++] = child;
				}
			}
			branches = new HashMap<T, Node>();
			for (int i = 0; i < children.length; i++) {
				if (hasMerged[i])
					continue;
				LinkedHashSet<T> edge = childToTags.get(children[i]);
				Node merged = children[i];
				hasMerged[i] = true;
				for (int j = i + 1; j < children.length; j++) {
					System.out.println("mergeing");
					if (!hasMerged[j] && merged.isCompatibleTo(children[j])) {
						if (!checkMergeCriteria(children[j], merged))
							continue;
						Node newMerged = merged.merge(children[j]);
						edge.addAll(childToTags.get(children[j]));
						System.out.println(" => "+newMerged);
						hasMerged[j] = true;
					}
				}
				for (T tag: edge)
					branches.put(tag, merged);
			}
		}
		@Override
		Node skipNoChoice() {
			HashMap<Node, LinkedHashSet<T>> childToTags = makeChildToTagsMap(branches);	
			if (childToTags.size() == 1) {
				return childToTags.keySet().iterator().next().skipNoChoice();
			}
			HashMap<Node, Node> replace = new HashMap<Node, Node>();
			for (Node before: childToTags.keySet()) {
				Node after = before.skipNoChoice();
				replace.put(before, after);
			}
			for (T tag: branches.keySet()) {
				Node before = branches.get(tag);
				branches.replace(tag, replace.get(before));
			}
			return this;
		}
	}
	static class TagPairNode extends TagNode<TagPairNode.TagPair> {
		static class TagPair {
			@Override
			public int hashCode() {
				return (op1.getValue() << 8) + op2.getValue();
			}
			@Override
			public boolean equals(Object obj) {
				if (obj == null)
					return false;
				if (!(obj instanceof TagPair))
					return false;
				TagPair other = (TagPair) obj;
				return op1 == other.op1 && op2 == other.op2;
			}
			PT op1;
			PT op2;
			TagPair(PT op1, PT op2) {
				this.op1 = op1;
				this.op2 = op2;
			}
		};
		TagPairNode() {
			super(-1);
		}
		@Override
		Object accept(NodeVisitor visitor) {
			return visitor.visitTagPairNode(this);
		}
		@Override
		boolean isCompatibleTo(Node otherx) {
			// Since TagPair is used only as a root node, if any, isCompatibleTo will
			// not be called.
			throw new Error("isCompatibleTo for TagPairNode is called");
			//return false;
		}
		@Override
		Node merge(Node otherx) {
			throw new Error("merge for TagPairNode is called");
		}
	}
	static class PTNode extends TagNode<PT> {
		PTNode(int opIndex) {
			super(opIndex);
		}
		@Override
		Object accept(NodeVisitor visitor) {
			return visitor.visitPTNode(this);
		}
		boolean doIsCompatibleTo(Node otherx) {
			if (!(otherx instanceof PTNode))
				return false;
			PTNode other = (PTNode) otherx;
			if (opIndex != other.opIndex)
				return false;
			return hasCompatibleBranches(other);		
		}
		@Override
		boolean isCompatibleTo(Node otherx) {
			boolean result = doIsCompatibleTo(otherx);
			System.out.println("isCompatible("+this+","+otherx+") = "+result);
			return result;
		}
		@Override
		Node merge(Node otherx) {
			PTNode other = (PTNode) otherx;
			PTNode merged = new PTNode(opIndex);
			merged.makeMergedNode(this, other);
			return merged;
		}
	}
	static class HTNode extends TagNode<HT> {
		boolean noHT;
		Node child;
		HTNode(int opIndex) {
			super(opIndex);
			noHT = false;
		}
		@Override
		void addBranch(TreeDigger digger, HT tag) {
			if (tag == null) {
				if (branches.size() != 0)
					throw new Error("invalid tag assignment");
				noHT = true;
				child = digger.dig(child);
			} else
				super.addBranch(digger, tag);
		}
		@Override
		Object accept(NodeVisitor visitor) {
			return visitor.visitHTNode(this);
		}
		@Override
		ArrayList<Node> getChildren() {
			if (noHT) {
				ArrayList<Node> r = new ArrayList<Node>(1);
				r.add(child);
				return r;
			}
			return super.getChildren();
		}
		boolean isNoHT() {
			return noHT;
		}
		Node getChild() {
			return child;
		}
		boolean doIsCompatibleTo(Node otherx) {
			if (!(otherx instanceof HTNode))
				return false;
			HTNode other = (HTNode) otherx;
			if (opIndex != other.opIndex)
				return false;
			if (noHT != other.noHT)
				return false;
			if (noHT && this.child.isCompatibleTo(other.child))
				return true;
			return hasCompatibleBranches(other);			
		}
		@Override
		boolean isCompatibleTo(Node otherx) {
			boolean result = doIsCompatibleTo(otherx);
			System.out.println("isCompatible("+this+","+otherx+") = "+result);
			return result;
		}
		@Override
		Node merge(Node otherx) {
			HTNode other = (HTNode) otherx;
			if (noHT) {
				HTNode merged = new HTNode(opIndex);
				merged.noHT = true;
				merged.child = child.merge(other.child);
				return merged;
			}
			HTNode merged = new HTNode(opIndex);
			merged.makeMergedNode(this, other);
			return merged;
		}
		@Override
		void mergeChildren() {
			if (noHT)
				child.mergeChildren();
			else
				super.mergeChildren();
		}
		@Override
		Node skipNoChoice() {
			if (noHT)
				return child.skipNoChoice();
			return super.skipNoChoice();
		}
	}
	
	static class TreeDigger {
		final LLRuleSet.LLRule rule;
		final VMRepType[] rts;
		final int arity;
		int planIndex;
		
		TreeDigger(LLRuleSet.LLRule r) {
			rule = r;
			rts = r.getVMRepTypes();
			arity = rts.length;
			planIndex = 0;
		}
		
		Node dig(Node nodex) {
			if (planIndex == DISPATCH_PLAN.length)
				return new Leaf(rule);
			
			int dispatchType = DISPATCH_PLAN[planIndex++];
			if (dispatchType == DISPATCH_TAGPAIR && arity == 2) {
				TagPairNode node = nodex == null ? new TagPairNode() : (TagPairNode) nodex;
				node.addBranch(this, new TagPairNode.TagPair(rts[0].getPT(), rts[1].getPT()));
				return node;
			} else if (DISPATCH_PT_BASE <= dispatchType &&
					   dispatchType < DISPATCH_HT_BASE &&
					   dispatchType - DISPATCH_PT_BASE < arity) {
				int opIndex = dispatchType - DISPATCH_PT_BASE;
				PTNode node = nodex == null ? new PTNode(opIndex) : (PTNode) nodex;
				node.addBranch(this, rts[opIndex].getPT());
				return node;
			} else if (DISPATCH_HT_BASE <= dispatchType &&
					   dispatchType - DISPATCH_HT_BASE < arity) {
				int opIndex = dispatchType - DISPATCH_HT_BASE;
				HTNode node = nodex == null ? new HTNode(opIndex) : (HTNode) nodex;
				node.addBranch(this, rts[opIndex].getHT());
				return node;
			} else
				throw new Error("invalid dispatch plan:"+dispatchType);
		}
	}
	
	Node root;
	
	public DecisionDiagram(LLRuleSet rs) {
		if (rs.getRules().size() == 0)
			return;
		for (LLRuleSet.LLRule r : rs.getRules()) {
			TreeDigger digger = new TreeDigger(r);
			root = digger.dig(root);
		}
		
//		System.out.println(generateCode(new String[] {"b1", "b2"}));

		root.mergeChildren();
		
//		System.out.println(generateCode(new String[] {"a1", "a2"}));
		
		root = root.skipNoChoice();

		//root.mergeChildren();
	}
	
	public String generateCode(String[] varNames) {
		return generateCodeForNode(root, varNames);
	}
	
	static String generateCodeForNode(Node node, String[] varNames) {
		CodeGenerateVisitor gen = new CodeGenerateVisitor(varNames);
		node.accept(gen);
		return gen.toString();
	}
	
	// precondition: a.isCompatibleTo(b)
	static boolean checkMergeCriteria(Node a, Node b) {
		if (a.isSingleLeafTree() && b.isSingleLeafTree())
			try {
				if (a.depth() != b.depth())
					throw new Error("depth does not match");
				return a.isAbsobable(b);
			} catch (Error e) {
				System.out.println("-----\n");
				System.out.println(generateCodeForNode(a, new String[]{"a", "b"}));
				System.out.println("---\n");
				System.out.println(generateCodeForNode(b, new String[]{"a", "b"}));
				System.out.println("-----\n");
				throw e;
			}
		if (MERGE_LEVEL == 0) {
			return !(a.isSingleLeafTree() || b.isSingleLeafTree());
		} else if (MERGE_LEVEL <= 1) {
			if (a.isSingleLeafTree())
				return b.isAbsobable(a);
			if (b.isSingleLeafTree())
				return a.isAbsobable(a);
		}
		return true;
	}
	
	static class CodeGenerateVisitor extends NodeVisitor {
		StringBuffer sb = new StringBuffer();
		String[] varNames;
		public CodeGenerateVisitor(String[] varNames) {
			this.varNames = varNames;
		}
		@Override
		public String toString() {
			return sb.toString();
		}
		@Override
		Object visitLeaf(Leaf node) {
			sb.append("{");
			if (DEBUG_COMMENT) {
				sb.append(" //");
				for (VMRepType rt: node.getRule().getVMRepTypes())
					sb.append(" ").append(rt.getName());
			}
			sb.append(node.getRule().getHLRule().action).append("}\n");
			return null;
		}
		@Override
		Object visitTagPairNode(TagPairNode node) {
			HashMap<Node, LinkedHashSet<TagPairNode.TagPair>> childToTags = node.getChildToTagsMap();
			sb.append("switch(TAG_PAIR("+varNames[0]+","+varNames[1]+")){");
			if (DEBUG_COMMENT)
				sb.append(" // "+this+"("+childToTags.size()+")");
			sb.append('\n');
			for (Node child: childToTags.keySet()) {
				for (TagPairNode.TagPair tag: childToTags.get(child))
					sb.append("case TAG_PAIR("+tag.op1.getName()+","+tag.op2.getName()+"):\n");
				child.accept(this);
				sb.append("break;\n");
			}
			sb.append("}");
			if (DEBUG_COMMENT)
				sb.append(" // "+this);
			sb.append('\n');
			return null;
		}
		@Override
		Object visitPTNode(PTNode node) {
			HashMap<Node, LinkedHashSet<PT>> childToTags = node.getChildToTagsMap();
			sb.append("switch(GET_PTAG("+varNames[node.getOpIndex()]+")){");
			if (DEBUG_COMMENT)
				sb.append(" // "+this+"("+childToTags.size()+")");
			sb.append('\n');
			for (Node child: childToTags.keySet()) {
				for (PT tag: childToTags.get(child))
					sb.append("case "+tag.getName()+":\n");
				child.accept(this);
				sb.append("break;\n");
			}
			sb.append("}");
			if (DEBUG_COMMENT)
				sb.append(" // "+this);
			sb.append('\n');
			return null;
		}
		@Override
		Object visitHTNode(HTNode node) {
			if (node.isNoHT()) {
				node.getChild().accept(this);
				return null;
			}
			HashMap<Node, LinkedHashSet<HT>> childToTags = node.getChildToTagsMap();
			sb.append("switch(GET_HTAG("+varNames[node.getOpIndex()]+")){");
			if (DEBUG_COMMENT)
				sb.append(" // "+this+"("+childToTags.size()+")");
			sb.append('\n');
			for (Node child: childToTags.keySet()) {
				for (HT tag: childToTags.get(child)) 
					sb.append("case "+tag.getName()+":\n");
				child.accept(this);
				sb.append("break;\n");
			}
			sb.append("}");
			if (DEBUG_COMMENT)
				sb.append("// "+this);
			sb.append('\n');
			return null;
		}
	}
}
